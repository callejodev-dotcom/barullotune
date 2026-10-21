// Proba do detector de ton en condicións difíciles, con ton verdadeiro coñecido.
// Só o motor (sen JUCE): cada caso é unha voz sintética cunha melodía e mídese, mostra a
// mostra, o ton que detecta a análise fronte ao verdadeiro.
//
//   BTDetectorTest [--json saida.json]
//
// Métricas por caso:
//   erro   % de tramas con voz (na verdade e no detector) a máis de medio semitono do ton
//          verdadeiro (erros grosos: oitavas, quintas, notas veciñas)
//   oitava % de tramas a unha oitava (±0,5) do verdadeiro
//   perde  % de tramas con voz verdadeira nas que o detector di que non hai voz (sen
//          contar os primeiros 60 ms de cada nota)
//   falsa  % de tramas sen voz (silencio ou só ruído) nas que o detector di que hai (sen
//          o caso da reverberación: alí a cola da nota anterior ten ton de verdade)
#include "tune/Engine.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include "Aleatorio.h"
#include <string>

namespace {
constexpr double SR = 48000.0;

struct Vowel { double f[4], b[4], g[4]; };
const Vowel VA { { 730, 1090, 2440, 3400 }, { 90, 110, 160, 250 }, { 1.0, 0.5, 0.25, 0.1 } };
const Vowel VI { { 270, 2290, 3010, 3600 }, { 60, 120, 160, 250 }, { 1.0, 0.25, 0.2, 0.1 } };
const Vowel VU { { 300, 870, 2240, 3400 },  { 60, 90, 150, 250 },  { 1.0, 0.35, 0.08, 0.05 } };

double envAt (const Vowel& v, double hz)
{
    double s = 0;
    for (int i = 0; i < 4; ++i) { const double d = (hz - v.f[i]) / (v.b[i] * 0.5); s += v.g[i] / std::sqrt (1.0 + d * d); }
    return s;
}

struct Note { double midi, dur; double gap; int vowel; };

struct Case
{
    std::string name;
    std::vector<Note> notes;
    double h1 = 1.0, h2 = 1.0;     // ganancia dos dous primeiros harmónicos
    double hnr = 1e9;              // harmónico/ruído aspirado (lineal, potencia)
    double snr = 1e9;              // ruído rosa de fondo
    double reverb = 0.0;           // nivel da reverberación (0 = sen)
    double vib = 0.2;              // fondo do vibrato (semitonos)
    bool fric = false;             // consoantes xordas («s», «x», «f») nos silencios
    double maxHz = 0;              // harmónicos ata aquí (0: ata 0,45·SR)
    uint32_t seed = 0;             // semente do sinal (0: a da orde no banco)
    int range = tune::Analysis::General;
};

struct Signal { std::vector<float> x; std::vector<double> truth; std::vector<char> fric; };   // truth < 0: sen voz

Signal synth (const Case& cs, uint32_t seed)
{
    Signal s;
    std::mt19937 rng (seed);
    aleatorio::Normal g (0, 1);
    double ph = 0, jit = 0;
    for (const auto& nt : cs.notes)
    {
        const Vowel& V = nt.vowel == 0 ? VA : nt.vowel == 1 ? VI : VU;
        const int N = (int) (nt.dur * SR), G = (int) (nt.gap * SR);
        for (int i = 0; i < N; ++i)
        {
            const double t = i / SR;
            double m = nt.midi + cs.vib * std::min (1.0, t / 0.3) * std::sin (2 * tune::kPi * 5.5 * t);
            jit = 0.999 * jit + 0.001 * g (rng);
            m += 0.3 * jit;
            const double f0 = 440.0 * std::pow (2.0, (m - 69) / 12.0);
            ph += f0 / SR; if (ph > 1e6) ph -= 1e6;
            double v = 0;
            const int H = (int) ((cs.maxHz > 0 ? std::min (cs.maxHz, 0.45 * SR) : 0.45 * SR) / f0);
            for (int h = 1; h <= H; ++h)
            {
                const double gh = h == 1 ? cs.h1 : h == 2 ? cs.h2 : 1.0;
                v += gh * envAt (V, h * f0) / h * std::sin (2 * tune::kPi * h * ph);
            }
            const double env = std::min (1.0, t / 0.03) * std::min (1.0, (nt.dur - t) / 0.03);
            s.x.push_back ((float) (0.1 * v * env));
            s.truth.push_back (m);
        }
        for (int i = 0; i < G; ++i) { s.x.push_back (0.0f); s.truth.push_back (-1); }
    }
    // potencia da voz (para fixar o ruído)
    double pv = 0; int nv = 0;
    for (size_t i = 0; i < s.x.size(); ++i) if (s.truth[i] >= 0) { pv += (double) s.x[i] * s.x[i]; ++nv; }
    pv /= std::max (1, nv);
    if (cs.hnr < 1e8)
    {
        // aire: ruído filtrado pola envolvente (paso baixo suave), só cando hai voz
        double lp = 0;
        const double a = 1.0 - std::exp (-2 * tune::kPi * 3000.0 / SR);
        std::vector<double> nz (s.x.size());
        double pn = 0;
        for (size_t i = 0; i < s.x.size(); ++i) { lp += (g (rng) - lp) * a; nz[i] = s.truth[i] >= 0 ? lp : 0.0; pn += nz[i] * nz[i]; }
        pn /= std::max (1, nv);
        const double k = std::sqrt (pv / cs.hnr / std::max (1e-20, pn));
        for (size_t i = 0; i < s.x.size(); ++i) s.x[i] += (float) (k * nz[i]);
    }
    if (cs.reverb > 0)
    {
        // reverberación: resposta de ruído con caída exponencial (RT60 1,2 s), mesturada
        const int L = (int) (1.2 * SR);
        std::vector<float> ir ((size_t) L);
        for (int i = 0; i < L; ++i) ir[(size_t) i] = (float) (g (rng) * std::exp (-6.9 * i / (double) L));
        double e = 0; for (float v : ir) e += (double) v * v;
        const float norm = (float) (cs.reverb / std::sqrt (e));
        std::vector<float> y (s.x.size(), 0.0f);
        // convolución dispersa (cada 4 mostras da IR, abonda para unha cola difusa)
        for (int k = 0; k < L; k += 4)
        {
            const float h = ir[(size_t) k] * norm * 2.0f;
            if (h == 0) continue;
            for (size_t i = (size_t) k; i < y.size(); ++i) y[i] += h * s.x[i - (size_t) k];
        }
        for (size_t i = 0; i < y.size(); ++i) s.x[i] += y[i];
    }
    if (cs.fric)
    {
        // consoantes xordas en cada silencio, ao nivel da voz menos 6 dB: «s» (4–9 kHz), «x»
        // (2–5 kHz) e «f» (1–8 kHz, máis feble). O detector non pode dicir que teñen ton
        s.fric.assign (s.x.size(), 0);
        size_t i = 0; int kind = 0;
        while (i < s.x.size())
        {
            if (s.truth[i] >= 0) { ++i; continue; }
            size_t j = i; while (j < s.x.size() && s.truth[j] < 0) ++j;
            const double lo = kind == 0 ? 4000 : kind == 1 ? 2000 : 1000, hi = kind == 0 ? 9000 : kind == 1 ? 5000 : 8000;
            const double lvl = kind == 2 ? 0.25 : 0.5;   // potencia relativa á da voz
            tune::Biquad hp1, lp1; hp1.lowpass (lo, SR); lp1.lowpass (hi, SR);
            std::vector<double> nz (j - i); double pn = 1e-20;
            for (size_t k = 0; k < nz.size(); ++k) { const double w = g (rng); const double l = hp1.process (w); nz[k] = lp1.process (w - l); pn += nz[k] * nz[k]; }
            pn /= (double) nz.size();
            const double kk = std::sqrt (pv * lvl / pn);
            for (size_t k = 0; k < nz.size(); ++k)
            {
                const double fade = std::min (1.0, std::min ((double) k, (double) (nz.size() - k)) / (0.01 * SR));
                s.x[i + k] += (float) (kk * nz[k] * fade);
                s.fric[i + k] = 1;
            }
            i = j; kind = (kind + 1) % 3;
        }
    }
    if (cs.snr < 1e8)
    {
        // ruído rosa (Voss-McCartney simplificado: tres polos)
        double b0 = 0, b1 = 0, b2 = 0, pn = 0;
        std::vector<double> nz (s.x.size());
        for (size_t i = 0; i < s.x.size(); ++i)
        {
            const double w = g (rng);
            b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
            nz[i] = b0 + b1 + b2 + w * 0.1848; pn += nz[i] * nz[i];
        }
        pn /= (double) s.x.size();
        const double k = std::sqrt (pv / cs.snr / pn);
        for (size_t i = 0; i < s.x.size(); ++i) s.x[i] += (float) (k * nz[i]);
    }
    return s;
}

std::vector<Note> melody (double base)
{
    // saltos de terceira, quinta, oitava e segunda; notas de 0,25 a 0,7 s, algunhas legato
    const double iv[] = { 0, 4, 7, 12, 11, 7, 5, 4, 2, 0, -3, 0, 5, 9, 7, 0 };
    const double du[] = { 0.5, 0.3, 0.4, 0.7, 0.25, 0.3, 0.45, 0.3, 0.3, 0.6, 0.35, 0.3, 0.4, 0.5, 0.3, 0.7 };
    const double gp[] = { 0, 0.08, 0, 0.12, 0, 0, 0.05, 0, 0, 0.2, 0, 0.06, 0, 0, 0.1, 0.3 };
    std::vector<Note> v;
    for (int i = 0; i < 16; ++i) v.push_back ({ base + iv[i], du[i], gp[i], i % 3 });
    return v;
}

// notas curtas con silencios de 0,2 s (para as consoantes)
std::vector<Note> melodyGaps (double base)
{
    const double iv[] = { 0, 4, 7, 12, 11, 7, 5, 4, 2, 0, -3, 0 };
    std::vector<Note> v;
    for (int i = 0; i < 12; ++i) v.push_back ({ base + iv[i], 0.3, 0.2, i % 3 });
    return v;
}

// casos difíciles ao chou, cada un da súa semente (para buscar fallos raros e repetilos):
// paseo de intervalos, notas de 0,2 a 0,6 s, silencios, aire, ruído, consoantes, vibrato
Case hardCase (int id)
{
    std::mt19937 rng (4242u + (uint32_t) id);
    Case c; c.name = "dificil_" + std::to_string (id);
    const double base = 40.0 + (double) (rng() % 37);
    double m = base;
    for (int i = 0; i < 14; ++i)
    {
        const int iv[] = { -7, -5, -4, -3, -2, -1, 1, 2, 3, 4, 5, 7, 12, -12 };
        m = std::max (base - 5, std::min (base + 12, m + iv[rng() % 14]));
        const double dur = 0.2 + 0.4 * (rng() % 1000) / 1000.0, gap = (rng() % 3 == 0) ? 0.0 : 0.25 * (rng() % 1000) / 1000.0;
        c.notes.push_back ({ m, dur, gap, (int) (rng() % 3) });
    }
    const double hnrs[] = { 1e9, 20.0, 10.0, 5.0, 3.0, 2.0 }, snrs[] = { 1e9, 1e9, 20.0, 10.0, 6.0 };
    c.hnr = hnrs[rng() % 6]; c.snr = snrs[rng() % 5]; c.fric = rng() % 2 == 0;
    c.vib = 0.1 + 0.5 * (rng() % 100) / 100.0;
    if (rng() % 5 == 0) { c.h1 = 0.3; c.h2 = 2.0; }
    c.maxHz = 8000;
    c.seed = 7000u + (uint32_t) id;
    return c;
}

struct Res { double err = 0, oct = 0, up = 0, miss = 0, falseV = 0; long outOfRange = 0; };

Res evaluate (const Case& cs, const Signal& s)
{
    tune::Engine e;
    e.prepare (SR);
    e.configure (tune::Engine::Studio, cs.range);
    // o detector vai atrasado (a súa xanela): compárase co ton verdadeiro de hai ~14 ms
    double lo, hi; tune::Analysis::rangeHz (cs.range, lo, hi);
    const int lag = (int) std::lround (SR / lo);
    long nErr = 0, nOct = 0, nUp = 0, nBoth = 0, nMiss = 0, nTruth = 0, nFalse = 0, nSil = 0, outOfRange = 0;
    int sinceOnset = 0;
    for (size_t i = 0; i < s.x.size(); ++i)
    {
        float y;
        e.processFrame (&s.x[i], &y, 1);
        // con voz, o ton sempre finito e dentro do rango (un período negativo colgaba as marcas);
        // no detector mesmo, que a análise xa non deixa pasar un período que non vale
        const auto& det = e.detector();
        if (det.voiced && ! (std::isfinite (det.period) && det.period > 0.0 && SR / det.period >= lo / 1.5 && SR / det.period <= hi * 1.5)) ++outOfRange;
        if ((i % 48) != 0 || (int) i < lag) continue;   // unha trama por milisegundo
        const double tr = s.truth[i - (size_t) lag];
        const bool tv = tr >= 0;
        // inicio de nota (para non contar o tempo que tarda en recoñecela)
        if (tv && (i - (size_t) lag < 48 || s.truth[i - (size_t) lag - 48] < 0)) sinceOnset = 0;
        sinceOnset += 1;
        if (tv)
        {
            if (sinceOnset > 60) { ++nTruth; if (! e.isVoiced()) ++nMiss; }
            if (e.isVoiced())
            {
                ++nBoth;
                const double d = e.inputMidi() - tr;
                if (std::abs (d) > 0.5) ++nErr;
                if (std::abs (std::abs (d) - 12.0) < 0.5) { ++nOct; if (d > 0) ++nUp; }
            }
        }
        else if (cs.fric)
        {
            // nunha consoante, despois de 30 ms (o que tarda en irse a vogal da xanela)
            bool past = true;
            for (int k = 1; k <= 30 && past; ++k) if (i - (size_t) lag >= (size_t) k * 48 && s.truth[i - (size_t) lag - (size_t) k * 48] >= 0) past = false;
            if (past && s.fric[i - (size_t) lag]) { ++nSil; if (e.isVoiced()) ++nFalse; }
        }
        else
        {
            // sen voz: só despois de 150 ms (a reverberación pode seguir soando)
            bool quiet = true;
            for (int k = 1; k <= 150 && quiet; ++k) if (i - (size_t) lag >= (size_t) k * 48 && s.truth[i - (size_t) lag - (size_t) k * 48] >= 0) quiet = false;
            if (quiet) { ++nSil; if (e.isVoiced()) ++nFalse; }
        }
    }
    Res r;
    r.err = 100.0 * nErr / std::max (1L, nBoth);
    r.oct = 100.0 * nOct / std::max (1L, nBoth);
    r.up = 100.0 * nUp / std::max (1L, nBoth);
    r.miss = 100.0 * nMiss / std::max (1L, nTruth);
    r.falseV = 100.0 * nFalse / std::max (1L, nSil);
    r.outOfRange = outOfRange;
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::string json;
    for (int a = 1; a + 1 < argc; ++a) if (std::strcmp (argv[a], "--json") == 0) json = argv[a + 1];
    // --buscar a b: os casos difíciles a..b, só para ver se algún dá un ton fóra do rango
    for (int a = 1; a + 2 < argc; ++a)
        if (std::strcmp (argv[a], "--buscar") == 0)
        {
            long total = 0;
            for (int id = std::atoi (argv[a + 1]); id <= std::atoi (argv[a + 2]); ++id)
            {
                const Case c = hardCase (id);
                const auto r = evaluate (c, synth (c, c.seed));
                if (r.outOfRange > 0) std::printf ("caso difícil %d: %ld mostras fóra do rango\n", id, r.outOfRange);
                total += r.outOfRange;
            }
            return total > 0 ? 1 : 0;
        }

    std::vector<Case> cases;
    auto add = [&] (const char* name, double base, auto&& fn) { Case c; c.name = name; c.notes = melody (base); fn (c); cases.push_back (c); };
    add ("limpa",            57, [] (Case&) {});
    add ("h2_dominante",     57, [] (Case& c) { c.h1 = 0.15; c.h2 = 2.5; });
    add ("sen_fundamental",  50, [] (Case& c) { c.h1 = 0.0; c.h2 = 0.5; });
    add ("aireada",          60, [] (Case& c) { c.hnr = 3.0; });
    add ("ruido_10db",       57, [] (Case& c) { c.snr = 10.0; });
    add ("ruido_5db",        57, [] (Case& c) { c.snr = 3.16; });
    add ("reverb",           57, [] (Case& c) { c.reverb = 0.7; });
    add ("home_grave",       40, [] (Case& c) { c.vib = 0.15; });
    add ("soprano_aguda",    72, [] (Case& c) { c.vib = 0.3; });
    add ("vibrato_ancho",    62, [] (Case& c) { c.vib = 0.7; });
    // máis voz aireada (para non axustar a un só caso): menos aire, grave e aguda
    add ("aireada_10db",     60, [] (Case& c) { c.hnr = 10.0; });
    add ("aireada_grave",    48, [] (Case& c) { c.hnr = 3.0; });
    add ("aireada_aguda",    69, [] (Case& c) { c.hnr = 3.0; });
    // consoantes xordas entre vogais: non poden ter ton (a voz aireada tampouco as pode coller)
    add ("consoantes",       57, [] (Case& c) { c.notes = melodyGaps (57); c.fric = true; });
    add ("consoantes_aire",  60, [] (Case& c) { c.notes = melodyGaps (60); c.fric = true; c.hnr = 3.0; });
    // moito aire, ruído e consoantes: aquí un candidato dos múltiplos que non era val de d
    // daba un período negativo e as marcas de ciclo quedaban nun bucle sen fin
    add ("aireada_ruido",    64, [] (Case& c) { c.notes = melodyGaps (64); c.fric = true; c.hnr = 2.0; c.snr = 4.0; c.vib = 0.4; });
    // dous casos difíciles nos que a 0.2.1 daba períodos negativos (atopados con --buscar)
    cases.push_back (hardCase (7)); cases.push_back (hardCase (43));

    std::map<std::string, double> metrics;
    std::printf ("%-18s %7s %7s %7s %7s %7s\n", "caso", "erro", "oitava", "(arriba)", "perde", "falsa");
    double tErr = 0, tMiss = 0, tFalse = 0; int nFalse = 0; long bad = 0;
    for (size_t k = 0; k < cases.size(); ++k)
    {
        const auto sig = synth (cases[k], cases[k].seed != 0 ? cases[k].seed : 100 + (uint32_t) k);
        const auto r = evaluate (cases[k], sig);
        std::printf ("%-18s %6.2f%% %6.2f%% %6.2f%%  %6.2f%% %6.2f%%\n", cases[k].name.c_str(), r.err, r.oct, r.up, r.miss, r.falseV);
        if (r.outOfRange > 0) { std::printf ("   FALLO: %ld mostras con ton fóra do rango\n", r.outOfRange); bad += r.outOfRange; }
        metrics["detector." + cases[k].name + ".erro"] = r.err;
        metrics["detector." + cases[k].name + ".perde"] = r.miss;
        // coa reverberación, o «silencio» ten a cola da nota anterior, que si ten ton
        if (cases[k].reverb <= 0) { metrics["detector." + cases[k].name + ".falsa"] = r.falseV; tFalse += r.falseV; ++nFalse; }
        tErr += r.err; tMiss += r.miss;
    }
    const double n = (double) cases.size();
    std::printf ("%-18s %6.2f%% %7s %7s  %6.2f%% %6.2f%%\n", "media", tErr / n, "", "", tMiss / n, tFalse / std::max (1, nFalse));
    metrics["detector.media.erro"] = tErr / n;
    metrics["detector.media.perde"] = tMiss / n;
    metrics["detector.media.falsa"] = tFalse / std::max (1, nFalse);
    if (! json.empty())
    {
        FILE* f = std::fopen (json.c_str(), "w");
        if (f)
        {
            std::fprintf (f, "{\n");
            size_t i = 0;
            for (auto& kv : metrics) std::fprintf (f, "  \"%s\": %.4f%s\n", kv.first.c_str(), kv.second, ++i < metrics.size() ? "," : "");
            std::fprintf (f, "}\n");
            std::fclose (f);
        }
    }
    return bad > 0 ? 1 : 0;
}
