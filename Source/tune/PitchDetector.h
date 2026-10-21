// Barullo Tune — detector de ton tipo YIN (sinal diezmada a ~12 kHz, cada ~4 ms).
#pragma once
#include "Dsp.h"

namespace tune {

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

} // namespace tune
