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
        cum.assign ((size_t) tauAlloc + 4, 0.0);
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

        d[0] = 1.0; cum[0] = 0;
        double run = 0;
        for (int t = 1; t <= tauMax; ++t)
        {
            double s = 0;
            for (int j = 0; j < win; ++j) { const double e = at (j) - at (j + t); s += e * e; }
            run += s;
            cum[(size_t) t] = run;
            d[(size_t) t] = run > 0 ? s * t / run : 1.0;
        }
        frameStart = start;
        // o primeiro val por baixo do limiar (YIN clásico)
        int global = -1;
        for (int t = tauMin; t < tauMax; ++t)
            if (d[(size_t) t] < threshold)
            {
                while (t + 1 < tauMax && d[(size_t) t + 1] < d[(size_t) t]) ++t;
                global = t; break;
            }
        int best = -1;
        // continuidade: se xa había voz, mira primeiro arredor do período anterior (±15 %)
        // para non saltar de oitava no medio dunha nota. Pero se a voz cambiou de nota
        // (o val de agora está lonxe e é claramente máis fondo), o de arredor é un val
        // falso: antes quedaba enganchado nel uns 50 ms e a nota saía unha quinta abaixo
        if (voiced)
        {
            const double pt = period / decim;
            const int lo = std::max (tauMin, (int) std::floor (pt * 0.87)), hi = std::min (tauMax - 1, (int) std::ceil (pt * 1.15));
            int bl = -1;
            for (int t = lo; t <= hi; ++t)
                if (t > 1 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (bl < 0 || d[(size_t) t] < d[(size_t) bl])) bl = t;
            const bool globalClearlyBetter = global >= 0 && (global < lo || global > hi) && d[(size_t) global] < d[(size_t) std::max (bl, 0)] - 0.1;
            if (bl >= 0 && d[(size_t) bl] < threshold + 0.12 && ! globalClearlyBetter) best = bl;
        }
        const bool following = best >= 0;
        if (best < 0) best = global;
        if (best < 0)
        {
            // ningún val por baixo do limiar: o máis fondo, pero só un val de verdade (no
            // bordo do rango, con ruído, a curva adoita seguir baixando e non é un período)
            best = -1;
            for (int t = tauMin + 1; t < tauMax - 1; ++t)
                if (d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (best < 0 || d[(size_t) t] < d[(size_t) best])) best = t;
            if (best < 0 || d[(size_t) best] > 0.35)
            {
                clarity = best < 0 ? 0.0 : 1.0 - d[(size_t) best];
                // unha soa lectura mala non apaga a voz (mantén o período anterior)
                if (voiced && ++misses < 2) return;
                voiced = false; pending = 0; return;
            }
        }
        // A oitava: entre o val elixido e os da súa familia (a metade, o dobre, o cuádruplo
        // do período), quédase o máis curto que sexa case tan fondo coma o máis fondo. Así
        // non se queda unha oitava arriba cando o fundamental é feble (unha vogal co primeiro
        // formante no segundo harmónico: a voz é case periódica a medio período), nin unha
        // abaixo (o val ao dobre do período é sempre case tan fondo coma o verdadeiro). E,
        // como se mira en cada lectura, un erro solto non queda enganchado pola continuidade.
        if (best > 0) best = following ? octaveSwitch (best) : octaveFamily (best);
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

    // seguindo unha nota (continuidade), a oitava só cambia con probas claras. (A regla
    // da familia enteira, en cada lectura, facía saltos soltos nas voces graves reais.)
    int octaveSwitch (int best) const
    {
        const double here = fracDepth (best);
        for (double mlt : { 2.0, 0.5 })
        {
            const double t = best * mlt;
            const int lo = std::max (tauMin, (int) std::floor (t * 0.96) - 1), hi = std::min (tauMax - 1, (int) std::ceil (t * 1.04) + 1);
            if (lo > hi) continue;
            int at = lo;
            for (int k = lo; k <= hi; ++k) if (d[(size_t) k] < d[(size_t) at]) at = k;
            // ao dobre: claramente máis fondo; á metade: polo menos igual de fondo (o período
            // máis curto gaña os empates, coma no YIN; con ruído o dobre e o verdadeiro empatan)
            const double there = fracDepth (at);
            if (at > lo && at < hi && (mlt > 1.0 ? there < 0.5 * here - 0.01 : there <= here)) return at;
        }
        return best;
    }

    int octaveFamily (int best) const
    {
        int cand[8]; double dep[8]; int nc = 0;
        auto dipAround = [this] (double t, int& at) -> bool
        {
            const int lo = std::max (tauMin, (int) std::floor (t * 0.96) - 1), hi = std::min (tauMax - 1, (int) std::ceil (t * 1.04) + 1);
            if (lo > hi) return false;
            at = lo;
            for (int k = lo; k <= hi; ++k) if (d[(size_t) k] < d[(size_t) at]) at = k;
            return at > lo && at < hi;   // un val de verdade, non o bordo dunha costa
        };
        // de curto a longo: os submúltiplos (con ruído, sen ningún val por baixo do limiar,
        // o máis fondo adoita ser un múltiplo longo: 3 ou 4 períodos) e o dobre e o cuádruplo
        const double mult[8] = { 1.0 / 5.0, 1.0 / 4.0, 1.0 / 3.0, 0.5, 1.0, 2.0, 3.0, 4.0 };
        for (double mlt : mult)
        {
            int at = best;
            if (mlt == 1.0 || dipAround (best * mlt, at)) { cand[nc] = at; dep[nc] = fracDepth (at); ++nc; }
        }
        double dmin = 1e9;
        for (int i = 0; i < nc; ++i) dmin = std::min (dmin, dep[i]);
        // «case tan fondo»: o dobre do máis fondo e un pouco (con ruído todos son menos
        // fondos; os que non son múltiplos do período quedan arredor de 1)
        for (int i = 0; i < nc; ++i)
            if (dep[i] <= 2.0 * dmin + 0.02) return cand[i];
        return best;
    }

    // fondo dun val medido no atraso fraccionario (o vértice da parábola), coa sinal
    // interpolada (Lagrange cúbica). Nun período curto (voz aguda) o val cae entre dúas
    // mostras e, en mostras enteiras ou coa parábola, parece menos fondo do que é
    double fracDepth (int t) const
    {
        if (t <= 1 || t + 2 >= (int) d.size()) return depth (t);
        const double a = d[(size_t) t - 1], b = d[(size_t) t], c = d[(size_t) t + 1], den = a - 2 * b + c;
        const double off = den > 0 ? clampd (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        const double tau = t + off;
        const int ti = (int) std::floor (tau);
        const double f = tau - ti;
        // pesos de Lagrange para x[ti-1], x[ti], x[ti+1], x[ti+2]
        const double w0 = -f * (f - 1) * (f - 2) / 6.0, w1 = (f + 1) * (f - 1) * (f - 2) / 2.0,
                     w2 = -(f + 1) * f * (f - 2) / 2.0, w3 = (f + 1) * f * (f - 1) / 6.0;
        auto at = [this] (int64_t i) { return (double) buf[(size_t) ((frameStart + i) & (size - 1))]; };
        double s = 0;
        for (int j = 0; j < win; ++j)
        {
            const int64_t k = j + ti;
            const double xi = w0 * at (k - 1) + w1 * at (k) + w2 * at (k + 1) + w3 * at (k + 2);
            const double e = at (j) - xi;
            s += e * e;
        }
        const double run = cum[(size_t) ti] + (cum[(size_t) ti + 1] - cum[(size_t) ti]) * f;
        return run > 0 ? s * tau / run : 1.0;
    }

    // fondo dun val coa parábola polas tres mostras
    double depth (int t) const
    {
        if (t <= 0 || t + 1 >= (int) d.size()) return d[(size_t) std::max (0, t)];
        const double a = d[(size_t) t - 1], b = d[(size_t) t], c = d[(size_t) t + 1], den = a - 2 * b + c;
        return den > 0 ? b - (a - c) * (a - c) / (8.0 * den) : b;
    }

    double sr = 48000, dsr = 12000;
    int decim = 4, tauMin = 2, tauMax = 200, win = 200, size = 1024, hop = 48;
    std::vector<float> buf;
    std::vector<double> d, cum;
    int64_t frameStart = 0;
    Biquad lp1, lp2;
    double hp = 0, hpPrev = 0;
    int64_t pos = 0;
    int dcount = 0, hcount = 0;
    double pending = 0;
    int misses = 0;
};

} // namespace tune
