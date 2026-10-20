// Barullo Tune — motor de corrección de afinación.
//
// Detección de ton tipo YIN (sobre a sinal diezmada a ~12 kHz, cada 4 ms) e dous
// xeitos de mover o ton:
//
//  * Directo (latencia < 1 ms): a voz lese a outra velocidade e, cando o punteiro de
//    lectura se adianta ou se atrasa, salta exactamente un ciclo cun fundido curto
//    (o método do afinador clásico). Para correccións pequenas os formantes nin se
//    notan; ao transpor moito, soa a esquío.
//  * Estudio (latencia 16–34 ms segundo o rango): TD-PSOLA. Mantén os formantes e
//    permite movelos á parte.
//
// Sen reservas de memoria no fío de audio: todo se reserva en prepare().
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tune {

constexpr double kPi = 3.14159265358979323846;   // M_PI non existe en MSVC

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fc, double sr, double q = 0.7071)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

static inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- YIN
class PitchDetector
{
public:
    static constexpr double allocMinHz = 60.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::lround (sr / 12000.0));
        dsr = sr / decim;
        const int tauAlloc = (int) std::ceil (dsr / allocMinHz) + 2;
        size = 1; while (size < 2 * tauAlloc + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        d.assign ((size_t) tauAlloc + 4, 0.0);
        lp1.lowpass (dsr * 0.42, sr, 0.54); lp2.lowpass (dsr * 0.42, sr, 1.31);
        hop = std::max (1, (int) std::lround (dsr * 0.004));   // ~4 ms
        setRange (75.0, 1100.0);
    }

    void setRange (double minHz, double maxHz)
    {
        tauMin = std::max (2, (int) std::floor (dsr / maxHz));
        tauMax = std::min ((int) d.size() - 3, (int) std::ceil (dsr / minHz) + 1);
        win = tauMax;
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lp1.reset(); lp2.reset();
        hp = 0; hpPrev = 0; pos = 0; dcount = 0; hcount = 0;
        period = sr / 220.0; voiced = false; clarity = 0; pending = 0; misses = 0;
    }

    // devolve true cando hai unha estimación nova
    bool push (float x)
    {
        // pasa altos suave (quita DC e retumbo) + paso baixo antialias
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

    double hopSeconds() const { return (double) hop * decim / sr; }

    double period = 0;   // en mostras da frecuencia orixinal
    bool voiced = false;
    double clarity = 0;  // 1 - mínimo da diferenza normalizada
    double threshold = 0.2, gate = 1.0e-9;   // ~ −90 dBFS: a voz decídea a periodicidade, non o nivel (antes −40 dBFS deixaba sen afinar as voces baixas)

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
            if (pending > 0 && std::abs (std::log2 (p / pending)) < 0.05) { period = p; voiced = true; misses = 0; }
            pending = p;
            return;
        }
        period = p;
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
    double pending = 0;
    int misses = 0;
};

// ---------------------------------------------------------------- formantes sen latencia
// Ao ler a voz a outra velocidade (Directo e Preciso) os formantes móvense co ton.
// Isto mide a envolvente espectral (predición lineal) da voz orixinal e da lida, cada
// ~5 ms e só co xa escoitado, e coa voz lida fai: branquear coa súa propia envolvente
// e volver colorear coa envolvente orixinal (ou desprazada co mando de formantes).
// Os filtros son de celosía e os coeficientes de reflexión interpólanse mostra a mostra,
// así sempre son estables.
class FormantFix
{
public:
    static constexpr int maxOrder = 40;

    void prepare (double sr)
    {
        order = std::max (16, std::min (maxOrder, (int) std::lround (sr / 1300.0)));
        frame = 1; while (frame < (int) (0.02 * sr)) frame <<= 1;
        hop = frame / 4;
        win.assign ((size_t) frame, 0.0f);
        for (int i = 0; i < frame; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (i + 0.5) / frame));
        lagW.assign ((size_t) maxOrder + 1, 0.0);
        for (int k = 0; k <= maxOrder; ++k) { const double a = 2.0 * 3.14159265358979323846 * 60.0 * k / sr; lagW[(size_t) k] = std::exp (-0.5 * a * a); }
        yRing.assign ((size_t) frame, 0.0f);
        xf.assign ((size_t) frame, 0.0f);
        wf.assign ((size_t) frame, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (yRing.begin(), yRing.end(), 0.0f);
        for (auto* k : { kx0, kx1, ky0, ky1 }) std::fill (k, k + maxOrder + 1, 0.0);
        for (auto& st : stateF) std::fill (std::begin (st), std::end (st), 0.0);
        for (auto& st : stateI) std::fill (std::begin (st), std::end (st), 0.0);
        g0 = g1 = 1.0; pos = 0; count = 0; amount = 0.0; ready = false;
    }

    // a voz lida (centro) entra aquí mostra a mostra; devolve true cando toca analizar
    bool pushY (float v)
    {
        yRing[(size_t) (pos++ & (frame - 1))] = v;
        if (++count >= hop) { count = 0; return true; }
        return false;
    }

    // analiza: xFrame = as últimas «frame» mostras da voz orixinal (xa lidas ao ritmo dos formantes)
    void analyse (const float* xFrame)
    {
        std::copy (kx1, kx1 + order + 1, kx0); std::copy (ky1, ky1 + order + 1, ky0); g0 = g1;
        double ex = 1, ey = 1;
        const bool okx = lpc (xFrame, 0, kx1, ex);
        for (int i = 0; i < frame; ++i) xf[(size_t) i] = yRing[(size_t) ((pos + i) & (frame - 1))];
        const bool oky = lpc (xf.data(), 0, ky1, ey);
        if (! okx || ! oky) { std::fill (kx1, kx1 + order + 1, 0.0); std::fill (ky1, ky1 + order + 1, 0.0); g1 = 1.0; }
        else g1 = std::sqrt (std::max (1e-6, ex) / std::max (1e-6, ey));
        g1 = std::min (4.0, std::max (0.25, g1));
        if (! ready) { std::copy (kx1, kx1 + order + 1, kx0); std::copy (ky1, ky1 + order + 1, ky0); g0 = g1; ready = true; }
        step = 0;
    }

    // filtra unha mostra da canle c: branquea coa envolvente lida e colorea coa orixinal
    float process (int c, float s)
    {
        const double t = (double) step / hop;
        double f = s;
        double* bF = stateF[c];
        // celosía FIR (A_y): f_m = f_{m-1} + k_m b_{m-1}(n-1); b_m(n) = b_{m-1}(n-1) + k_m f_{m-1}
        double bPrev = f;   // b_0(n)
        for (int m = 1; m <= order; ++m)
        {
            const double k = ky0[m] + (ky1[m] - ky0[m]) * t;
            const double fNew = f + k * bF[m - 1];
            const double bNew = bF[m - 1] + k * f;
            bF[m - 1] = bPrev;
            bPrev = bNew;
            f = fNew;
        }
        // celosía IIR (1/A_x)
        double* bI = stateI[c];
        double e = f;
        for (int m = order; m >= 1; --m)
        {
            const double k = kx0[m] + (kx1[m] - kx0[m]) * t;
            e = e - k * bI[m - 1];
            bI[m] = bI[m - 1] + k * e;
        }
        bI[0] = e;
        if (c == 0 && step < hop) ++step;
        const double g = g0 + (g1 - g0) * t;
        const double out = e * g;
        return std::isfinite (out) ? (float) out : 0.0f;
    }

    int frame = 1024, hop = 128, order = 20;
    double amount = 0.0;   // 0 = sen corrección, 1 = corrección completa (para fundir)
    bool ready = false;
    std::vector<float> xf;  // marco de traballo (a orixinal, lida fóra)

private:
    // autocorrelación con xanela + Levinson. k[1..order] e erro relativo e = E/r0
    bool lpc (const float* x, int, double* k, double& e)
    {
        double r[maxOrder + 1];
        double* w = wf.data();
        for (int i = 0; i < frame; ++i) w[i] = (double) x[i] * win[(size_t) i];   // a xanela, unha soa vez
        for (int lag = 0; lag <= order; ++lag)
        {
            double acc = 0;
            for (int i = lag; i < frame; ++i) acc += w[i] * w[i - lag];
            r[lag] = acc * lagW[(size_t) lag];
        }
        if (r[0] < 1e-12) return false;
        r[0] *= 1.0001;
        double a[maxOrder + 1] = { 1.0 }, tmp[maxOrder + 1];
        for (int i = 1; i <= order; ++i) a[i] = 0;
        double err = r[0];
        for (int m = 1; m <= order; ++m)
        {
            double acc = r[m];
            for (int i = 1; i < m; ++i) acc += a[i] * r[m - i];
            const double km = -acc / err;
            if (! (std::abs (km) < 1.0)) return false;
            for (int i = 0; i <= m; ++i) tmp[i] = a[i];
            for (int i = 1; i < m; ++i) a[i] = tmp[i] + km * tmp[m - i];
            a[m] = km;
            k[m] = km;
            err *= (1.0 - km * km);
        }
        e = err / r[0];
        return true;
    }

    std::vector<float> win, yRing;
    std::vector<double> wf;
    std::vector<double> lagW;
    double kx0[maxOrder + 1] {}, kx1[maxOrder + 1] {}, ky0[maxOrder + 1] {}, ky1[maxOrder + 1] {};
    double stateF[2][maxOrder + 1] {}, stateI[2][maxOrder + 1] {};
    double g0 = 1, g1 = 1;
    int64_t pos = 0;
    int count = 0, step = 0;
};

// ---------------------------------------------------------------- motor
class Engine
{
public:
    enum Mode { ScaleMode = 0, MidiScale = 1, MidiNote = 2 };
    enum Quality { Direct = 0, Precise = 1, Studio = 2 };
    // tipos de voz: rango do detector (e latencia en estudio)
    enum Range { Bass = 0, Baritone, Tenor, Alto, Mezzo, Soprano, General, numRanges };

    // parámetros (pódense cambiar en calquera momento)
    double refA = 440.0;       // La4
    int key = 0;               // tónica 0 = Do
    uint16_t scale = 0x0FFF;   // bit i = grado i (cromática por defecto)
    uint16_t freeMask = 0;     // notas «libres»: cando a voz vai a elas, non se corrixe
    double speedMs = 0.0;      // canto tarda en afinar dentro dunha nota (0 = ao momento)
    double transitionMs = 0.0; // canto tarda en pasar dunha nota á seguinte
    double tolCents = 0.0;     // desafinacións menores ca isto non se tocan
    double tolMs = 0.0;        // e as maiores, só se duran máis ca isto
    double vibrato = 1.0;      // 0..2: canto vibrato da voz pasa á saída
    double amount = 1.0;       // 0..1 canto se corrixe
    int mode = ScaleMode;
    double transpose = 0.0;    // semitonos
    double formant = 0.0;      // semitonos (en Directo e Preciso, coa corrección de formantes)

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        detector.prepare (sr);
        decisionAlpha = 1.0 - std::exp (-detector.hopSeconds() / 0.04);
        pMaxAlloc = (int) std::ceil (sr / PitchDetector::allocMinHz) + 2;
        ringSize = 1; while (ringSize < 10 * pMaxAlloc + 256) ringSize <<= 1;
        in.assign ((size_t) ringSize, 0.0f);
        for (int c = 0; c < 2; ++c) { inCh[c].assign ((size_t) ringSize, 0.0f); outCh[c].assign ((size_t) ringSize, 0.0); }
        unvoicedP = std::max (16.0, std::round (sr * 0.005));
        xfade = std::max (16, (int) std::lround (sr * 0.0006));
        dMin = (int) std::ceil (1.1 * xfade) + 4 + sincHalf;   // o interpolador mira 8 mostras adiante
        buildSinc();
        ffix.prepare (sr);
        ffixAlpha = 1.0 - std::exp (-1.0 / (0.02 * sr));
        dirAlpha = 1.0 - std::exp (-1.0 / (0.0015 * sr));
        configure (quality, range);
    }

    // cambia o xeito e o rango (cambia a latencia; volve empezar)
    void configure (int q, int r)
    {
        quality = std::max (0, std::min (2, q));
        range = std::max (0, std::min ((int) numRanges - 1, r));
        double lo, hi;
        rangeHz (range, lo, hi);
        detector.setRange (lo, hi);
        pMax = (int) std::ceil (sr / lo) + 2;
        // preciso: a lectura vai 11,5 ms detrás, dentro de ciclos xa medidos (sen extrapolar)
        dLead = quality == Precise ? dMin + (int) std::lround (0.0115 * sr) : dMin;
        latencySamples = quality == Studio ? (int) std::ceil (2.5 * pMax) + 32 : dLead;
        reset();
    }

    static void rangeHz (int r, double& lo, double& hi)
    {
        //                                    baixo barítono tenor alto mezzo soprano calquera
        static const double mins[numRanges] = {  60,  75,  95, 130,  150,  180,   70 };
        static const double maxs[numRanges] = { 420, 520, 680, 900, 1050, 1350, 1100 };
        r = std::max (0, std::min ((int) numRanges - 1, r));
        lo = mins[r]; hi = maxs[r];
    }

    int latency() const { return latencySamples; }
    int currentQuality() const { return quality; }
    int currentRange() const { return range; }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        for (int c = 0; c < 2; ++c) { std::fill (inCh[c].begin(), inCh[c].end(), 0.0f); std::fill (outCh[c].begin(), outCh[c].end(), 0.0); }
        detector.reset();
        n = 0;
        // estudio
        nMarks = 0; firstMark = 0; lastMark = 0; lastMarkVoiced = false;
        nextSyn = (double) pMax; srcOff = -(double) latencySamples; synVoiced = false; studioState = {};
        // directo
        ffix.reset();
        rp = -(double) dLead; rp2 = 0; xfPos = -1; dirCorr = 0; rho = 1.0; cycleOk = false; directState = {};
        // común
        target = -1; mSmooth = 0;
        voiced = false; detectedHz = 0; correctionCents = 0; inputMidi = 0; outputMidi = 0;
    }

    void noteOn (int note) { if (note >= 0 && note < 128) { held[note] = 1; lastNote = note; recomputeHeld(); } }
    void noteOff (int note)
    {
        if (note < 0 || note >= 128) return;
        held[note] = 0; recomputeHeld();
        if (note == lastNote) { lastNote = -1; for (int i = 127; i >= 0; --i) if (held[i]) { lastNote = i; break; } }
    }
    void allNotesOff() { for (auto& h : held) h = 0; heldMask = 0; lastNote = -1; }

    float process (float x) { float y = 0; processFrame (&x, &y, 1); return y; }

    // unha mostra de 1 ou 2 canles: o ton detéctase no centro (L+R)/2 e as dúas canles
    // corríxense exactamente igual, así a imaxe estéreo non se move
    void processFrame (const float* x, float* y, int numChannels)
    {
        nch = numChannels >= 2 ? 2 : 1;
        const size_t w = (size_t) (n & (ringSize - 1));
        const float mid = nch == 1 ? x[0] : 0.5f * (x[0] + x[1]);
        in[w] = mid;
        for (int c = 0; c < nch; ++c) inCh[c][w] = x[c];
        if (detector.push (mid)) updatePitch();
        placeMarks();   // os dous xeitos seguen os ciclos da voz
        if (quality == Studio) studioSample (y); else directSample (y);
        ++n;
    }

    // para a interface
    bool voiced = false;
    double detectedHz = 0, correctionCents = 0, inputMidi = 0, outputMidi = 0;
    double clarity() const { return detector.clarity; }
    uint16_t heldPitchClasses() const { return heldMask; }
    int targetNote() const { return target; }

private:
    // ============================================================ común
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
        return ((scale | freeMask) >> deg) & 1;
    }

    bool isFree (int note) const
    {
        if ((mode == MidiScale || mode == MidiNote) && heldMask != 0) return false;
        const int deg = (((note % 12) + 12) % 12 - key + 12) % 12;
        return (freeMask >> deg) & 1;
    }

    void updatePitch()
    {
        const bool was = voiced;
        voiced = detector.voiced;
        if (! voiced) return;
        curP = detector.period;
        detectedHz = sr / curP;
        const double m = 69.0 + 12.0 * std::log2 (detectedHz / refA);
        // a nota de destino decídese co ton suavizado (sen vibrato), para que un vibrato
        // ancho ou unha voz a medio camiño entre dúas notas non faga saltar o destino
        // ton para decidir a nota: suavizado 40 ms
        const double dtd = detector.hopSeconds() * sr;
        if (! was) { mSmooth = m; candidate = -1; candHeld = 0; }
        else mSmooth += (m - mSmooth) * decisionAlpha;
        const double md = mSmooth;

        int t = target;
        if (mode == MidiNote && lastNote >= 0)
        {
            // nota exacta: a oitava máis próxima á voz
            t = lastNote;
            while (t - md > 6.0) t -= 12;
            while (md - t > 6.0) t += 12;
        }
        else
        {
            // nota permitida máis próxima, con histérese: unha nota nova acéptase se dura
            // 25 ms ou se o salto é claro (case un semitono); así as voces que oscilan entre
            // dúas notas non fan bailar o destino
            int best = -1; double bd = 1e9;
            const int c = (int) std::lround (md);
            for (int k = c - 7; k <= c + 7; ++k)
                if (allowed (k) && std::abs (md - k) < bd) { bd = std::abs (md - k); best = k; }
            if (best < 0) best = c;
            const bool keep = was && t >= 0 && allowed (t) && std::abs (md - t) < 0.6;
            if (! keep && best != t)
            {
                if (best != candidate) { candidate = best; candHeld = 0; }
                candHeld += dtd;
                if (! was || t < 0 || ! allowed (t) || std::abs (md - t) > 0.9 || candHeld >= 0.025 * sr) t = best;
            }
            else candidate = -1;
        }
        target = t;
        inputMidi = m;
    }

    // ---- o modelo de corrección (o mesmo para os dous xeitos) ----
    // O vibrato sácase cun filtro de banda (3–8 Hz) sobre o ton; o resto é o «centro» da voz.
    // O centro achégase á nota con «velocidade»; a nota de destino móvese dunha a outra
    // con «transición»; o vibrato volve sumarse multiplicado por «vibrato».
    struct CorrState
    {
        bool active = false; int lastTarget = -1;
        double tS = 0, cS = 0, vibRamp = 0, tolTimer = 0;
        double ic1 = 0, ic2 = 0;   // filtro de estado (TPT) do vibrato
    };

    double alphaFor (double ms, double dt) const { return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-dt / (ms * 0.001 * sr)); }

    // banda do vibrato: pasa banda de 5,5 Hz, Q 1,2 (ganancia 1 no centro); dt en mostras
    double vibratoBand (CorrState& st, double m, double dt) const
    {
        const double k = 1.0 / 1.2;
        const double gg = std::tan (kPi * std::min (0.45, 5.5 * dt / sr));
        const double a1 = 1.0 / (1.0 + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
        const double v3 = m - st.ic2;
        const double v1 = a1 * st.ic1 + a2 * v3, v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = 2.0 * v1 - st.ic1; st.ic2 = 2.0 * v2 - st.ic2;
        return k * v1;
    }

    // corrección en semitonos para unha voz en m (MIDI) que vai a tgt; dt = mostras desde o paso anterior
    double correctionStep (CorrState& st, double m, int tgt, double dt, double& outMidi)
    {
        const bool free = isFree (tgt);
        if (! st.active)
        {
            // comezo dunha nota: xa afinada
            st.active = true; st.tS = tgt; st.vibRamp = 0; st.tolTimer = 0;
            st.ic1 = 0; st.ic2 = m;
            const double err = st.tS - m;
            st.cS = (free || std::abs (err) * 100.0 < tolCents) ? 0.0 : err * amount;
            st.lastTarget = tgt;
            outMidi = m + st.cS + transpose;
            return st.cS + transpose;
        }
        if (tgt != st.lastTarget) st.vibRamp = 0;   // nota nova: o vibrato volve entrar amodo
        st.lastTarget = tgt;
        st.tS += (tgt - st.tS) * alphaFor (transitionMs, dt);
        st.vibRamp = std::min (1.0, st.vibRamp + dt / (0.10 * sr));
        const double vib = clampd (vibratoBand (st, m, dt), -1.5, 1.5) * st.vibRamp;
        const double centre = m - vib;
        const double err = st.tS - centre;
        double want = 0.0;
        if (! free)
        {
            const bool outside = std::abs (err) * 100.0 >= tolCents;
            st.tolTimer = outside ? st.tolTimer + dt : 0.0;
            if (outside && st.tolTimer >= tolMs * 0.001 * sr) want = err * amount;
        }
        st.cS += (want - st.cS) * alphaFor (speedMs, dt);
        outMidi = centre + st.cS + vibrato * vib + transpose;
        return outMidi - m;
    }

    // lectura entre mostras: sinc con xanela de Kaiser, 16 puntos (plana ata ~20 kHz).
    // A interpolación lineal apagaba os agudos (−3 dB a 14 kHz) e metía un ruído que
    // cambiaba co ton.
    static constexpr int sincHalf = 8, sincPhases = 512;
    void buildSinc()
    {
        sinc.assign ((size_t) (sincPhases + 1) * 2 * sincHalf, 0.0f);
        const double fc = 0.47, beta = 8.0;
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double i0b = bessel0 (beta);
        for (int ph = 0; ph <= sincPhases; ++ph)
        {
            const double f = (double) ph / sincPhases;
            double sum = 0;
            float* row = &sinc[(size_t) ph * 2 * sincHalf];
            for (int k = 0; k < 2 * sincHalf; ++k)
            {
                const double x = (double) (k - (sincHalf - 1)) - f;          // taps -7 … +8
                const double r = x / (double) sincHalf;
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                const double v = std::abs (x) < 1e-9 ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
                row[k] = (float) (v * w); sum += v * w;
            }
            for (int k = 0; k < 2 * sincHalf; ++k) row[k] = (float) (row[k] / sum);
        }
    }

    float readAt (double p) const { return readFrom (in, p); }
    float readFrom (const std::vector<float>& buf, double p) const
    {
        const int64_t i0 = (int64_t) std::floor (p);
        const double fp = (p - (double) i0) * sincPhases;
        const int ph = std::min (sincPhases - 1, (int) fp);
        const float fr = (float) (fp - ph);
        const float* r0 = &sinc[(size_t) ph * 2 * sincHalf];
        const float* r1 = r0 + 2 * sincHalf;
        float acc = 0.0f;
        const int64_t base = i0 - (sincHalf - 1);
        for (int k = 0; k < 2 * sincHalf; ++k)
            acc += (r0[k] + fr * (r1[k] - r0[k])) * buf[(size_t) ((base + k) & (ringSize - 1))];
        return acc;
    }

    // ============================================================ directo
    // Mellor salto (arredor de nominal, en ±range) para que o tramo que empeza en p+sign·j
    // se pareza ao que empeza en p: correlación normalizada sobre o fundido + parábola.
    double bestJump (double p, double nominal, double spread, double sign, double maxJump, int len)
    {
        const int lo = std::max (1, (int) std::floor (nominal - spread));
        const int hi = std::min ({ (int) std::floor (maxJump), (int) std::ceil (nominal + spread), lo + maxSearch - 1 });
        if (hi < lo) return std::max (1.0, std::min (nominal, maxJump));
        const int64_t base = (int64_t) std::floor (p);
        auto X = [this] (int64_t i) { return (double) in[(size_t) (i & (ringSize - 1))]; };
        double aa = 1e-12;
        for (int k = 0; k < len; ++k) aa += X (base + k) * X (base + k);
        int bj = lo; double best = -1e30;
        for (int j = lo; j <= hi; ++j)
        {
            const int64_t qb = base + (int64_t) (sign * j);
            double ab = 0, bb = 1e-12;
            for (int k = 0; k < len; ++k) { const double x1 = X (qb + k); ab += X (base + k) * x1; bb += x1 * x1; }
            const double sc = ab / std::sqrt (aa * bb);
            score[j - lo] = sc;
            if (sc > best) { best = sc; bj = j; }
        }
        double frac = 0;
        if (bj > lo && bj < hi)
        {
            const double y0 = score[bj - lo - 1], y1 = score[bj - lo], y2 = score[bj - lo + 1], den = y0 - 2 * y1 + y2;
            if (den < 0) frac = clampd (0.5 * (y0 - y2) / den, -0.5, 0.5);
        }
        return std::max (1.0, std::min (maxJump, (double) bj + frac));
    }

    void directSample (float* y)
    {
        // corrección mostra a mostra (co ton detectado agora mesmo)
        double raw = transpose;
        double mNow = inputMidi;
        if (voiced)
        {
            double mi = 0;
            if (pitchAtFromMarks (rp, mi)) mNow = mi;                 // xa medido: o ton dos ciclos onde se le
            else if (cycleOk)
                mNow = cycleM + cycleSlope * clampd (rp - cycleT, 0.0, 4.0 * curP);   // aínda non: extrapólase
        }
        const bool starting = voiced && target >= 0 && ! directState.active;
        if (voiced && target >= 0) { double om; raw = correctionStep (directState, mNow, target, 1.0, om); }
        else directState.active = false;
        if (starting) dirCorr = raw;
        else dirCorr += (raw - dirCorr) * dirAlpha;   // 1,5 ms de suavizado, para que non rasque
        rho = std::pow (2.0, dirCorr / 12.0);
        correctionCents = voiced ? (dirCorr - transpose) * 100.0 : 0.0;
        if (voiced) outputMidi = mNow + dirCorr;

        // saída: un punteiro (ou dous durante un fundido), o mesmo para cada canle
        for (int c = 0; c < nch; ++c) y[c] = readFrom (inCh[c], rp);
        if (xfPos >= 0)
        {
            const double w = 0.5 - 0.5 * std::cos (kPi * (xfPos + 0.5) / xfLen);
            for (int c = 0; c < nch; ++c) y[c] = (float) (y[c] * (1.0 - w) + readFrom (inCh[c], rp2) * w);
            rp2 += rho;
            if (++xfPos >= xfLen) { rp = rp2; xfPos = -1; }
        }
        rp += rho;

        // formantes: ao transpor (ou co mando de formantes) devolve o timbre da voz orixinal
        const bool want = std::abs (transpose) >= 0.99 || std::abs (formant) >= 0.05;
        ffix.amount += ((want ? 1.0 : 0.0) - ffix.amount) * ffixAlpha;
        if (ffix.amount < 1e-4 && ! want) { if (ffix.ready) ffix.reset(); }
        else
        {
            const float mid = nch == 1 ? y[0] : 0.5f * (y[0] + y[1]);
            if (ffix.pushY (mid))
            {
                // marco da orixinal, lido á velocidade dos formantes (máis rápido = formantes máis altos)
                const double phi = std::pow (2.0, clampd (formant, -7.0, 7.0) / 12.0);
                if (std::abs (formant) < 0.05)
                    for (int i = 0; i < ffix.frame; ++i) ffix.xf[(size_t) i] = in[(size_t) ((n - ffix.frame + 1 + i) & (ringSize - 1))];
                else
                {
                    const double start = (double) n - (double) ffix.frame * phi;
                    for (int i = 0; i < ffix.frame; ++i) ffix.xf[(size_t) i] = readFrom (in, start + i * phi);
                }
                ffix.analyse (ffix.xf.data());   // (analiza a orixinal antes de reutilizar o marco)
            }
            for (int c = 0; c < nch; ++c)
            {
                const float fixed = ffix.process (c, y[c]);
                y[c] = (float) (y[c] + (fixed - y[c]) * ffix.amount);
            }
        }

        // ¿fai falta saltar un ciclo?
        if (xfPos < 0)
        {
            const double D = (double) n - rp;     // atraso actual
            const double P = voiced ? curP : 2.0 * unvoicedP;
            // o salto escóllese comparando medio ciclo (o fundido é curto: así non mestura dous ciclos distintos)
            const int cmp = (int) clampd (P * 0.5, (double) xfade, 2048.0);
            xfLen = xfade;
            if (rho > 1.0 && D < (double) dLead)
            {
                // a lectura alcanza a escritura (ton para arriba): volve atrás un ciclo
                const double j = voiced ? bestJump (rp, P, P * 0.12, -1.0, (double) (ringSize / 2), std::min (cmp, (int) D - 2)) : P;
                rp2 = rp - j; xfPos = 0;
            }
            else if (rho <= 1.0 && D >= (double) dLead + 1.25 * P)
            {
                // a lectura quedou atrás (ton para abaixo): salta un ciclo adiante, así o atraso nunca medra
                const double maxJ = D - (double) dLead - 2.0;
                const double j = voiced ? bestJump (rp, P, P * 0.12, +1.0, maxJ, std::min (cmp, (int) (D - maxJ) - 2)) : std::min (P, maxJ);
                rp2 = rp + j; xfPos = 0;
            }
        }
    }

    // ============================================================ estudio (PSOLA)
    struct Mark { double pos; double period; bool voiced; int target; };

    void studioSample (float* y)
    {
        while (nextSyn <= (double) (n + pMax)) synthesize();
        const size_t o = (size_t) (n & (ringSize - 1));
        for (int c = 0; c < nch; ++c)
        {
            y[c] = (float) outCh[c][o];   // PSOLA clásico: sen normalizar (cada pulso conserva o seu nivel)
            outCh[c][o] = 0;
        }
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
            // para comparar fai falta medio ciclo despois da marca (o grano enteiro xa o mira estudio ao usala)
            if (c + search + std::max<int64_t> (8, (int64_t) (P * 0.5)) + 1 > n) break;
            double pos = (double) c;
            if (search > 0 && lastMarkVoiced && nMarks > 0)
            {
                // a marca nova vai ao mesmo punto do ciclo cá anterior: o desprazamento co que
                // o ciclo seguinte se parece máis ao actual (correlación normalizada + parábola)
                const int h = std::max (8, (int) (P * 0.5));
                const int nS = std::min (2 * search + 1, maxSearch);
                double best = -1e30; int bi = 0;
                for (int j = 0; j < nS; ++j)
                {
                    const int64_t i = c - search + j;
                    double ab = 0, bb = 1e-12;
                    for (int k = -h; k <= h; ++k) { const double x0 = X (last + k), x1 = X (i + k); ab += x0 * x1; bb += x1 * x1; }
                    score[j] = ab / std::sqrt (bb);
                    if (score[j] > best) { best = score[j]; bi = j; }
                }
                double frac = 0;
                if (bi > 0 && bi < nS - 1)
                {
                    const double y0 = score[bi - 1], y1 = score[bi], y2 = score[bi + 1], den = y0 - 2 * y1 + y2;
                    if (den < 0) frac = clampd (0.5 * (y0 - y2) / den, -0.5, 0.5);
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
            updateCycle();
        }
        // tira as marcas vellas
        while (nMarks > 2 && marks[(size_t) firstMark].pos < (double) (n - latencySamples - 4 * pMax))
        { firstMark = (firstMark + 1) % maxMarks; --nMarks; }
    }

    const Mark& markAt (int k) const { return marks[(size_t) ((firstMark + k) % maxMarks)]; }

    // ton na posición p a partir dos ciclos xa marcados: interpola entre os centros dos
    // ciclos veciños. Devolve false se p aínda non está entre dúas marcas con voz.
    bool pitchAtFromMarks (double p, double& m) const
    {
        int k = nMarks - 1;
        while (k >= 1 && markAt (k).pos > p) --k;   // markAt(k).pos <= p
        if (k < 1 || k + 1 >= nMarks) return false;
        auto periodOf = [this] (int i) -> double     // intervalo entre a marca i e a i+1
        {
            if (i < 0 || i + 1 >= nMarks) return 0;
            const Mark& a = markAt (i); const Mark& b = markAt (i + 1);
            if (! a.voiced || ! b.voiced) return 0;
            const double per = b.pos - a.pos;
            return (per < curP * 0.8 || per > curP * 1.25) ? 0.0 : per;
        };
        const double pk = periodOf (k);
        if (pk <= 0) return false;
        auto toMidi = [this] (double per) { return 69.0 + 12.0 * std::log2 (sr / per / refA); };
        const double ck = markAt (k).pos + 0.5 * pk;
        const int nb = p < ck ? k - 1 : k + 1;          // intervalo veciño cara a onde está p
        const double pn = periodOf (nb);
        if (pn <= 0) { m = toMidi (pk); return true; }
        const double cn = markAt (nb).pos + 0.5 * pn;
        const double t = clampd ((p - ck) / (cn - ck), 0.0, 1.0);
        m = toMidi (pk) + (toMidi (pn) - toMidi (pk)) * t;
        return true;
    }

    // ton dos dous últimos ciclos medidos e a súa tendencia (para o xeito directo, que
    // le a voz case ao momento e non pode esperar polo detector)
    void updateCycle()
    {
        // recta axustada ao ton dos últimos ciclos (mínimos cadrados): o ton e a tendencia
        // sen o tremor ciclo a ciclo dunha voz real
        cycleOk = false;
        constexpr int K = 3;
        if (nMarks < K + 1) return;
        double t[K], m[K];
        for (int i = 0; i < K; ++i)
        {
            const Mark& a = markAt (nMarks - 1 - i); const Mark& b = markAt (nMarks - 2 - i);
            if (! (a.voiced && b.voiced)) return;
            const double p = a.pos - b.pos;
            if (p < curP * 0.92 || p > curP * 1.09) return;   // máis de semitono e medio do detector: marca dubidosa
            t[i] = 0.5 * (a.pos + b.pos);
            m[i] = 69.0 + 12.0 * std::log2 (sr / p / refA);
        }
        double tm = 0, mm = 0;
        for (int i = 0; i < K; ++i) { tm += t[i]; mm += m[i]; }
        tm /= K; mm /= K;
        double num = 0, den = 0;
        for (int i = 0; i < K; ++i) { num += (t[i] - tm) * (m[i] - mm); den += (t[i] - tm) * (t[i] - tm); }
        double res = 0;
        for (int i = 0; i < K; ++i) { const double e = m[i] - (mm + (den > 0 ? num / den : 0.0) * (t[i] - tm)); res += e * e; }
        if (std::sqrt (res / K) > 0.12) return;   // ciclos que non casan (máis de 12 cents fóra da recta): mellor o detector
        const double maxSlope = 25.0 / sr;   // 25 semitonos por segundo, como moito
        cycleSlope = den > 0 ? clampd (num / den, -maxSlope, maxSlope) : 0.0;
        cycleM = mm; cycleT = tm; cycleOk = true;
    }

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
        const double fr = std::pow (2.0, clampd (formant, -7.0, 7.0) / 12.0);   // lectura máis rápida = formantes máis altos
        const double reach = std::max (1.0, fr);
        auto ready = [&] (const Mark& m2) { return m2.pos + std::ceil (m2.period * (m2.voiced ? reach : 1.0)) + 2 + sincHalf <= (double) n; };
        // marca máis próxima a ti que xa teña todo o grano dispoñible
        int bestK = -1; double bd = 1e18;
        for (int k = 0; k < nMarks; ++k)
        {
            const Mark& mk = markAt (k);
            if (! ready (mk)) continue;
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
                if (! m2.voiced || ! ready (m2)) continue;
                const double dd = std::abs (m2.pos - want);
                if (dd < bd2) { bd2 = dd; bestK = k; }
            }
            const double ns = markAt (bestK).pos - srcOff;
            if (ns > (double) n + 1.0) nextSyn = ns;
        }
        const Mark mk = markAt (bestK);

        double P = mk.period, spacing = P, rf = 1.0;
        if (mk.voiced && mk.target >= 0)
        {
            // cada grano co seu propio ton: a corrección calcúlase co período deste grano,
            // así a saída cae xusto na nota aínda que a voz se estea movendo
            P = localPeriod (bestK);
            const double m = 69.0 + 12.0 * std::log2 (sr / P / refA);
            double om = m;
            const double corr = correctionStep (studioState, m, mk.target, P, om);
            synVoiced = true;
            spacing = P / std::pow (2.0, corr / 12.0);
            correctionCents = (corr - transpose) * 100.0;
            outputMidi = om;
            rf = fr;
        }
        else
        {
            // xordo: OLA sen cambio de tempo
            P = unvoicedP; spacing = P;
            synVoiced = false; studioState.active = false; correctionCents = 0;
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
            srcOff += clampd (nominal - srcOff, -step, step);
            src = nextSyn + srcOff;
        }
        if (mk.voiced && synVoiced) srcOff = mk.pos - nextSyn;
        for (int k = -half; k <= half; ++k)
        {
            const double off = k - frac;          // distancia ao centro do grano (na saída)
            if (std::abs (off) >= P) continue;
            if (c + k < n) continue;
            const double w = 0.5 * (1.0 + std::cos (kPi * off / P));
            const double sp = src + off * rf;
            const int64_t i0 = (int64_t) std::floor (sp);
            if (i0 + sincHalf > n || i0 < n - ringSize + 2 * sincHalf) continue;
            for (int ch = 0; ch < nch; ++ch) outCh[ch][(size_t) ((c + k) & (ringSize - 1))] += readFrom (inCh[ch], sp) * w;
        }
        nextSyn += spacing;
    }

    static constexpr int maxMarks = 512, maxSearch = 512;
    double score[maxSearch] {};

    double sr = 48000;
    PitchDetector detector;
    int quality = Direct, range = General;
    int pMax = 640, pMaxAlloc = 800, latencySamples = 64, ringSize = 8192;
    int xfade = 36, dMin = 44, dLead = 44;
    double unvoicedP = 240;
    std::vector<float> in, sinc;          // in: o centro (para detectar e marcar)
    std::vector<float> inCh[2];           // cada canle
    std::vector<double> outCh[2];
    int nch = 1;
    int64_t n = 0;

    // estudio
    Mark marks[maxMarks] {};
    int firstMark = 0, nMarks = 0;
    double lastMark = 0; bool lastMarkVoiced = false;
    double nextSyn = 0, srcOff = 0;
    CorrState studioState, directState;
    bool synVoiced = false;

    // directo
    double rp = 0, rp2 = 0, dirCorr = 0, rho = 1.0, dirAlpha = 0.01;
    double cycleM = 0, cycleT = 0, cycleSlope = 0, ffixAlpha = 0.001;
    FormantFix ffix;
    bool cycleOk = false;
    int xfPos = -1, xfLen = 36;

    // común
    double curP = 200, mSmooth = 0, decisionAlpha = 0.1, candHeld = 0;
    int candidate = -1;
    int target = -1, lastNote = -1;
    uint8_t held[128] {};
    uint16_t heldMask = 0;
};

} // namespace tune
