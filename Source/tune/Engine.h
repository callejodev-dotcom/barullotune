// Barullo Tune — motor de corrección de afinación.
// Detección de tono tipo YIN (sobre la señal diezmada a ~12 kHz) y
// desplazamiento por TD-PSOLA en tiempo real, que mantiene los formantes.
// Latencia fija, sin reservas de memoria en el hilo de audio.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tune {

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fc, double sr, double q = 0.7071)
    {
        const double w = 2.0 * M_PI * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ---------------------------------------------------------------- YIN
class PitchDetector
{
public:
    void prepare (double sampleRate, double minHz, double maxHz)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::lround (sr / 12000.0));
        dsr = sr / decim;
        tauMin = std::max (2, (int) std::floor (dsr / maxHz));
        tauMax = (int) std::ceil (dsr / minHz) + 1;
        win = tauMax;
        size = 1; while (size < win + tauMax + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        d.assign ((size_t) tauMax + 2, 0.0);
        lp1.lowpass (dsr * 0.42, sr, 0.54); lp2.lowpass (dsr * 0.42, sr, 1.31);
        hp = 0; hpPrev = 0;
        hop = std::max (1, (int) std::lround (dsr * 0.004)); // ~4 ms
        pos = 0; dcount = 0; hcount = 0;
        period = sr / 220.0; voiced = false; clarity = 0; pending = 0; misses = 0;
    }

    // devuelve true cuando hay una estimación nueva
    bool push (float x)
    {
        // pasa altos suave (quita DC y retumbe) + paso bajo antialias
        const double h = x - hpPrev + 0.995 * hp; hpPrev = x; hp = h;
        const double y = lp2.process (lp1.process (h));
        if (++dcount < decim) return false;
        dcount = 0;
        buf[(size_t) (pos++ & (size - 1))] = (float) y;
        if (++hcount < hop) return false;
        hcount = 0;
        analyse();
        return true;
    }

    double period = 0;   // en muestras de la frecuencia original
    bool voiced = false;
    double clarity = 0;  // 1 - mínimo de la diferencia normalizada
    double threshold = 0.2, gate = 1.0e-4;
    double pending = 0;
    double hopSeconds() const { return (double) hop * decim / sr; }
    int misses = 0;

private:
    void analyse()
    {
        const int64_t start = pos - (win + tauMax);
        if (start < 0) { voiced = false; return; }
        auto at = [this, start] (int i) { return buf[(size_t) ((start + i) & (size - 1))]; };

        double energy = 0;
        for (int j = 0; j < win; ++j) energy += (double) at (j) * at (j);
        if (energy / win < gate) { voiced = false; pending = 0; clarity = 0; return; }

        d[0] = 1.0;
        double run = 0;
        for (int t = 1; t <= tauMax; ++t)
        {
            double s = 0;
            for (int j = 0; j < win; ++j) { const double e = at (j) - at (j + t); s += e * e; }
            run += s;
            d[(size_t) t] = run > 0 ? s * t / run : 1.0;
        }
        int best = -1;
        // continuidade: se xa había voz, mira primeiro arredor do período anterior (±15 %)
        // para non saltar de oitava no medio dunha nota
        if (voiced)
        {
            const double pt = period / decim;
            const int lo = std::max (tauMin, (int) std::floor (pt * 0.87)), hi = std::min (tauMax - 1, (int) std::ceil (pt * 1.15));
            int bl = -1;
            for (int t = lo; t <= hi; ++t)
                if (t > 1 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (bl < 0 || d[(size_t) t] < d[(size_t) bl])) bl = t;
            if (bl >= 0 && d[(size_t) bl] < threshold + 0.12) best = bl;
        }
        if (best < 0)
            for (int t = tauMin; t < tauMax; ++t)
                if (d[(size_t) t] < threshold)
                {
                    while (t + 1 < tauMax && d[(size_t) t + 1] < d[(size_t) t]) ++t;
                    best = t; break;
                }
        if (best < 0)
        {
            best = tauMin;
            for (int t = tauMin; t < tauMax; ++t) if (d[(size_t) t] < d[(size_t) best]) best = t;
            if (d[(size_t) best] > 0.35)
            {
                clarity = 1.0 - d[(size_t) best];
                // unha soa lectura mala non apaga a voz (mantén o período anterior)
                if (voiced && ++misses < 2) return;
                voiced = false; pending = 0; return;
            }
        }
        double tau = best;
        if (best > 1 && best < tauMax)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2 * b + c;
            if (den > 0) tau += 0.5 * (a - c) / den;
        }
        clarity = 1.0 - d[(size_t) best];
        const double p = tau * decim;
        if (! voiced)
        {
            // comezo de nota: fai falta que dúas lecturas seguidas coincidan (±0,6 semitonos)
            if (pending > 0 && std::abs (std::log2 (p / pending)) < 0.05) { period = p; voiced = true; }
            pending = p;
            return;
        }
        period = p;
        voiced = true;
        misses = 0;
    }

    double sr = 48000, dsr = 12000;
    int decim = 4, tauMin = 2, tauMax = 200, win = 200, size = 1024, hop = 48;
    std::vector<float> buf;
    std::vector<double> d;
    Biquad lp1, lp2;
    double hp = 0, hpPrev = 0;
    int64_t pos = 0;
    int dcount = 0, hcount = 0;
};

// ---------------------------------------------------------------- motor
class Engine
{
public:
    enum Mode { ScaleMode = 0, MidiScale = 1, MidiNote = 2 };

    // parámetros
    double refA = 440.0;       // La4
    int key = 0;               // tónica 0 = Do
    uint16_t scale = 0x0FFF;   // bit i = grado i (cromática por defecto)
    double speedMs = 0.0;      // 0 = robot; ~50-400 = natural
    double amount = 1.0;       // 0..1 cuánto se corrixe
    double humanize = 0.0;     // 0..1 notas largas se corrixen máis amodo
    int mode = ScaleMode;
    double transpose = 0.0;    // semitonos

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        detector.prepare (sr, minHz, maxHz);
        decisionAlpha = 1.0 - std::exp (-detector.hopSeconds() / 0.04);
        pMax = (int) std::ceil (sr / minHz) + 2;
        latencySamples = (int) std::ceil (2.5 * pMax) + 32;
        ringSize = 1; while (ringSize < latencySamples + 4 * pMax + 64) ringSize <<= 1;
        in.assign ((size_t) ringSize, 0.0f);
        out.assign ((size_t) ringSize, 0.0);
        unvoicedP = std::max (16.0, std::round (sr * 0.005));
        reset();
    }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (out.begin(), out.end(), 0.0);
        n = 0; nMarks = 0; firstMark = 0;
        lastMark = 0; lastMarkVoiced = false;
        nextSyn = (double) pMax; srcOff = -(double) latencySamples; synCorr = 0; synVoiced = false; effMs = 0; target = -1; heldMask = 0; lastNote = -1;
        for (auto& h : held) h = 0;
        stableSamples = 0;
        voiced = false; detectedHz = 0; correctionCents = 0;
    }

    int latency() const { return latencySamples; }

    void noteOn (int note) { if (note >= 0 && note < 128) { held[note] = 1; lastNote = note; recomputeHeld(); } }
    void noteOff (int note)
    {
        if (note < 0 || note >= 128) return;
        held[note] = 0; recomputeHeld();
        if (note == lastNote) { lastNote = -1; for (int i = 127; i >= 0; --i) if (held[i]) { lastNote = i; break; } }
    }
    void allNotesOff() { for (auto& h : held) h = 0; heldMask = 0; lastNote = -1; }

    float process (float x)
    {
        in[(size_t) (n & (ringSize - 1))] = x;

        if (detector.push (x)) updatePitch();
        smoothCorrection();
        placeMarks();
        while (nextSyn <= (double) (n + pMax)) synthesize();

        const size_t o = (size_t) (n & (ringSize - 1));
        const double y = out[o];   // PSOLA clásico: sen normalizar (cada pulso conserva o seu nivel)
        out[o] = 0;
        ++n;
        return (float) y;
    }

    // para la interfaz
    bool voiced = false;
    double detectedHz = 0, correctionCents = 0, inputMidi = 0, outputMidi = 0;
    uint16_t heldPitchClasses() const { return heldMask; }
    int targetNote() const { return target; }

private:
    struct Mark { double pos; double period; bool voiced; int target; };

    void recomputeHeld()
    {
        heldMask = 0;
        for (int i = 0; i < 128; ++i) if (held[i]) heldMask |= (uint16_t) (1u << (i % 12));
    }

    bool allowed (int note) const
    {
        const int pc = ((note % 12) + 12) % 12;
        if ((mode == MidiScale || mode == MidiNote) && heldMask != 0) return (heldMask >> pc) & 1;
        const int deg = ((pc - key) % 12 + 12) % 12;
        return (scale >> deg) & 1;
    }

    void updatePitch()
    {
        const bool was = voiced;
        voiced = detector.voiced;
        if (! voiced) { stableSamples = 0; return; }
        curP = detector.period;
        detectedHz = sr / curP;
        const double m = 69.0 + 12.0 * std::log2 (detectedHz / refA);
        // a nota de destino decídese co ton suavizado (sen vibrato), para que un vibrato
        // ancho ou unha voz a medio camiño entre dúas notas non faga saltar o destino
        if (! was) mSmooth = m;
        else mSmooth += (m - mSmooth) * decisionAlpha;
        const double md = mSmooth;

        int t = target;
        if (mode == MidiNote && lastNote >= 0)
        {
            // nota exacta: a octava máis próxima á voz
            t = lastNote;
            while (t - md > 6.0) t -= 12;
            while (md - t > 6.0) t += 12;
        }
        else
        {
            // nota permitida máis próxima, con histérese
            const bool keep = t >= 0 && allowed (t) && std::abs (md - t) < 0.6;
            if (! keep)
            {
                int best = -1; double bd = 1e9;
                const int c = (int) std::lround (md);
                for (int k = c - 7; k <= c + 7; ++k)
                    if (allowed (k) && std::abs (md - k) < bd) { bd = std::abs (md - k); best = k; }
                t = best >= 0 ? best : c;
            }
        }
        if (t != target || ! was) stableSamples = 0;
        target = t;
        inputMidi = m;
    }

    // tempo de corrección actual (humanizar: as notas longas relaxan a corrección)
    void smoothCorrection()
    {
        double ms = speedMs;
        if (voiced) stableSamples++;
        if (humanize > 0)
            ms += humanize * 150.0 * std::min (1.0, (stableSamples / sr) / 0.5);
        effMs = ms;
    }

    // marcas de análise (centros de grano), sincronizadas co ton e con precisión de fracción de mostra
    void placeMarks()
    {
        const double P = voiced ? curP : unvoicedP;
        auto X = [this] (int64_t i) { return in[(size_t) (i & (ringSize - 1))]; };
        for (;;)
        {
            const int64_t last = (int64_t) std::floor (lastMark);
            const double lastFrac = lastMark - (double) last;
            int64_t c = (nMarks == 0) ? n - (int64_t) (2 * P) : last + (int64_t) std::lround (P);
            const int search = voiced ? (int) (P * (lastMarkVoiced ? 0.2 : 0.5)) : 0;
            if (c + search + (int64_t) P + 1 > n) break;
            double pos = (double) c;
            if (search > 0 && lastMarkVoiced && nMarks > 0)
            {
                // a marca nova vai ao mesmo punto do ciclo cá anterior: o desprazamento co que
                // o ciclo seguinte se parece máis ao actual (correlación normalizada + parábola)
                const int h = std::max (8, (int) (P * 0.5));
                const int nS = 2 * search + 1;
                double best = -1e30; int bi = 0;
                for (int j = 0; j < nS && j < maxSearch; ++j)
                {
                    const int64_t i = c - search + j;
                    double ab = 0, bb = 1e-12;
                    for (int k = -h; k <= h; ++k) { const double x0 = X (last + k), x1 = X (i + k); ab += x0 * x1; bb += x1 * x1; }
                    score[j] = ab / std::sqrt (bb);
                    if (score[j] > best) { best = score[j]; bi = j; }
                }
                double frac = 0;
                if (bi > 0 && bi < std::min (nS, maxSearch) - 1)
                {
                    const double y0 = score[bi - 1], y1 = score[bi], y2 = score[bi + 1], den = y0 - 2 * y1 + y2;
                    if (den < 0) frac = juce_clamp (0.5 * (y0 - y2) / den, -0.5, 0.5);
                }
                pos = (double) (c - search + bi) + frac + lastFrac;
                pos = std::max (pos, lastMark + P * 0.6);
            }
            else if (search > 0)
            {
                // primeira marca dunha nota: no pico máis alto
                int64_t bestI = c; float bv = -1e9f;
                for (int64_t i = c - search; i <= c + search; ++i)
                {
                    const float v = std::abs (X (i));
                    if (v > bv) { bv = v; bestI = i; }
                }
                pos = (double) bestI;
                if (nMarks > 0 && pos <= lastMark) pos = lastMark + std::max (1.0, P * 0.5);
            }
            Mark& mk = marks[(size_t) ((firstMark + nMarks) % maxMarks)];
            mk = { pos, P, voiced, target };
            if (nMarks < maxMarks) ++nMarks; else firstMark = (firstMark + 1) % maxMarks;
            lastMark = pos; lastMarkVoiced = voiced;
        }
        // tira as marcas vellas
        while (nMarks > 2 && marks[(size_t) firstMark].pos < (double) (n - latencySamples - 3 * pMax))
        { firstMark = (firstMark + 1) % maxMarks; --nMarks; }
    }

    static double juce_clamp (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

    const Mark& markAt (int k) const { return marks[(size_t) ((firstMark + k) % maxMarks)]; }

    // período local dun grano: a distancia ás marcas veciñas (máis exacta có detector,
    // que vai uns milisegundos atrasado e cun vibrato rápido erra decenas de cents)
    double localPeriod (int k) const
    {
        const Mark& mk = markAt (k);
        double P = mk.period;
        const bool prev = k > 0 && markAt (k - 1).voiced;
        const bool next = k + 1 < nMarks && markAt (k + 1).voiced;
        if (prev && next) P = 0.5 * (markAt (k + 1).pos - markAt (k - 1).pos);
        else if (prev) P = mk.pos - markAt (k - 1).pos;
        else if (next) P = markAt (k + 1).pos - mk.pos;
        if (P < mk.period * 0.75 || P > mk.period * 1.33) P = mk.period;   // salto raro: fíase do detector
        return P;
    }

    void synthesize()
    {
        const double ti = nextSyn - latencySamples;
        // marca máis próxima a ti que xa teña todo o grano dispoñible
        int bestK = -1; double bd = 1e18;
        for (int k = 0; k < nMarks; ++k)
        {
            const Mark& mk = markAt (k);
            if (mk.pos + std::ceil (mk.period) + 2 > (double) n) continue;
            const double dd = std::abs (mk.pos - ti);
            if (dd < bd) { bd = dd; bestK = k; }
        }
        if (bestK < 0) { nextSyn += unvoicedP; return; }
        if (markAt (bestK).voiced && ! synVoiced)
        {
            // comezo dunha zona con voz: escolle a marca máis próxima a onde se estaba lendo
            // e move un pouco o grano de saída para que case sen salto co anterior
            const double want = nextSyn + srcOff;
            double bd2 = 1e18;
            for (int k = 0; k < nMarks; ++k)
            {
                const Mark& m2 = markAt (k);
                if (! m2.voiced || m2.pos + std::ceil (m2.period) + 2 > (double) n) continue;
                const double dd = std::abs (m2.pos - want);
                if (dd < bd2) { bd2 = dd; bestK = k; }
            }
            const double ns = markAt (bestK).pos - srcOff;
            if (ns > (double) n + 1.0) nextSyn = ns;
        }
        const Mark mk = markAt (bestK);

        double P = mk.period, spacing = P;
        if (mk.voiced && mk.target >= 0)
        {
            // cada grano co seu propio ton: a corrección calcúlase co período deste grano,
            // así a saída cae xusto na nota aínda que a voz se estea movendo
            P = localPeriod (bestK);
            const double m = 69.0 + 12.0 * std::log2 (sr / P / refA);
            const double raw = (mk.target - m) * amount + transpose;
            if (! synVoiced) synCorr = raw;   // ao comezar unha nota, xa afinada
            else
            {
                const double a = effMs <= 0.0 ? 1.0 : 1.0 - std::exp (-P / (effMs * 0.001 * sr));
                synCorr += (raw - synCorr) * a;
            }
            synVoiced = true;
            spacing = P / std::pow (2.0, synCorr / 12.0);
            correctionCents = synCorr * 100.0;
            outputMidi = m + synCorr;
        }
        else
        {
            // xordo: OLA sen cambio de tempo, collendo do mesmo instante
            P = unvoicedP; spacing = P;
            synVoiced = false; correctionCents = 0;
        }
        const int half = (int) std::ceil (P);
        const int64_t c = (int64_t) std::floor (nextSyn);
        const double frac = nextSyn - (double) c;
        double src;   // centro de lectura
        if (mk.voiced && synVoiced) src = mk.pos;
        else
        {
            // xordo: lectura continua desde onde quedou, achegándose amodo ao atraso nominal
            const double nominal = -(double) latencySamples;
            const double step = P * 0.08;
            srcOff += std::max (-step, std::min (step, nominal - srcOff));
            src = nextSyn + srcOff;
        }
        if (mk.voiced && synVoiced) srcOff = mk.pos - nextSyn;
        for (int k = -half; k <= half; ++k)
        {
            const double off = k - frac;          // distancia ao centro do grano
            if (std::abs (off) >= P) continue;
            const double w = 0.5 * (1.0 + std::cos (M_PI * off / P));
            const double sp = src + off;
            const int64_t i0 = (int64_t) std::floor (sp);
            const double f = sp - (double) i0;
            if (i0 + 1 > n || i0 < n - ringSize + 1) continue;
            const double a = in[(size_t) (i0 & (ringSize - 1))], b = in[(size_t) ((i0 + 1) & (ringSize - 1))];
            const size_t o = (size_t) ((c + k) & (ringSize - 1));
            if (c + k < n) continue;
            out[o] += (a + (b - a) * f) * w;
        }
        nextSyn += spacing;
    }

    static constexpr double minHz = 75.0, maxHz = 1100.0;
    static constexpr int maxMarks = 512, maxSearch = 512;
    double score[maxSearch] {};

    double sr = 48000;
    PitchDetector detector;
    int pMax = 640, latencySamples = 1700, ringSize = 8192;
    double unvoicedP = 240;
    std::vector<float> in;
    std::vector<double> out;
    int64_t n = 0;

    Mark marks[maxMarks] {};
    int firstMark = 0, nMarks = 0;
    double lastMark = 0; bool lastMarkVoiced = false;
    double nextSyn = 0;

    double curP = 200, effMs = 0, synCorr = 0, mSmooth = 0, decisionAlpha = 0.1;
    bool synVoiced = false;
    double srcOff = 0;
    int target = -1, lastNote = -1;
    uint8_t held[128] {};
    uint16_t heldMask = 0;
    int64_t stableSamples = 0;
};

} // namespace tune
