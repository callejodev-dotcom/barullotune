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
    int range = tune::Analysis::General;
};

struct Signal { std::vector<float> x; std::vector<double> truth; };   // truth < 0: sen voz

Signal synth (const Case& cs, uint32_t seed)
{
    Signal s;
    std::mt19937 rng (seed);
    std::normal_distribution<double> g (0, 1);
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
            const int H = (int) (0.45 * SR / f0);
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

struct Res { double err = 0, oct = 0, up = 0, miss = 0, falseV = 0; };

Res evaluate (const Case& cs, const Signal& s)
{
    tune::Engine e;
    e.prepare (SR);
    e.configure (tune::Engine::Studio, cs.range);
    // o detector vai atrasado (a súa xanela): compárase co ton verdadeiro de hai ~14 ms
    double lo, hi; tune::Analysis::rangeHz (cs.range, lo, hi);
    const int lag = (int) std::lround (SR / lo);
    long nErr = 0, nOct = 0, nUp = 0, nBoth = 0, nMiss = 0, nTruth = 0, nFalse = 0, nSil = 0;
    int sinceOnset = 0;
    for (size_t i = 0; i < s.x.size(); ++i)
    {
        float y;
        e.processFrame (&s.x[i], &y, 1);
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
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    std::string json;
    for (int a = 1; a + 1 < argc; ++a) if (std::strcmp (argv[a], "--json") == 0) json = argv[a + 1];

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

    std::map<std::string, double> metrics;
    std::printf ("%-18s %7s %7s %7s %7s %7s\n", "caso", "erro", "oitava", "(arriba)", "perde", "falsa");
    double tErr = 0, tMiss = 0, tFalse = 0; int nFalse = 0;
    for (size_t k = 0; k < cases.size(); ++k)
    {
        const auto sig = synth (cases[k], 100 + (uint32_t) k);
        const auto r = evaluate (cases[k], sig);
        std::printf ("%-18s %6.2f%% %6.2f%% %6.2f%%  %6.2f%% %6.2f%%\n", cases[k].name.c_str(), r.err, r.oct, r.up, r.miss, r.falseV);
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
    return 0;
}
