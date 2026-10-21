// Barullo Tune — detector de ton tipo YIN (sinal diezmada a ~12 kHz, cada ~4 ms).
#pragma once
#include "Dsp.h"
#include <cstdlib>
#include <cstdio>
static inline double btE (const char* k, double d) { const char* v = std::getenv (k); return v ? std::atof (v) : d; }

namespace tune {

class PitchDetector
{
public:
    static constexpr int numFeatures = 7;
    static inline bool voiceModel = true;   // (false: as regras de antes; para volcar datos e axustar o modelo)
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
        comb.assign ((size_t) tauAlloc + 4, 0.0);
        cum.assign ((size_t) tauAlloc + 4, 0.0);
        lp1.lowpass (dsr * 0.42, sr, 0.54); lp2.lowpass (dsr * 0.42, sr, 1.31);
        hfA = 1.0 / (1.0 + 2.0 * kPi * 4000.0 / sr);   // paso alto dun polo a 4 kHz
        lfBand.lowpass (1000.0, sr);
        envA = 1.0 - std::exp (-1.0 / (0.004 * sr));
        hop = std::max (1, (int) std::lround (dsr * 0.004));   // ~4 ms
        peakDecay = std::pow (10.0, -20.0 / 10.0 * hop * decim / sr);   // −20 dB/s
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
        lp1.reset(); lp2.reset(); lfBand.reset();
        hfX = hfY = hfEnv = lfEnv = 0; peakE = 0;
        hp = 0; hpPrev = 0; pos = 0; dcount = 0; hcount = 0;
        period = sr / 220.0; voiced = false; clarity = 0; pending = 0; pV = 0; anchor = 0; holds = 0; commonFrom = 0; commonAge = 0;
    }

    // devolve true cando hai unha estimación nova
    bool push (float x)
    {
        // pasa altos suave (quita DC e retumbo) + paso baixo antialias
        const double h = x - hpPrev + 0.995 * hp; hpPrev = x; hp = h;
        const double y = lp2.process (lp1.process (h));
        // agudos (por riba de 4 kHz) e graves (ata 1 kHz) á frecuencia orixinal: unha «s» ou
        // unha «x» ten case todo arriba, e despois de diezmar xa non se ve
        const double hf = hfA * (hfY + h - hfX); hfX = h; hfY = hf;
        const double lf = lfBand.process (h);
        hfEnv += (hf * hf - hfEnv) * envA; lfEnv += (lf * lf - lfEnv) * envA;
        if (++dcount < decim) return false;
        dcount = 0;
        buf[(size_t) (pos++ & (size - 1))] = (float) y;
        if (++hcount < hop) return false;
        hcount = 0;
        analyse();
        return true;
    }

    double hopSeconds() const { return (double) hop * decim / sr; }

    // o que se mira para decidir se hai voz (tamén para axustar o modelo fóra)
    struct Features { double x[numFeatures] {}; int cand = -1; bool accepted = false, valid = false; };
    Features feat;
    int64_t frames = 0;   // lecturas feitas (para ler feat unha vez por lectura)

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
        ++frames;
        frameEnergy = energy / win;
        peakE = std::max (frameEnergy, peakE * peakDecay);
        feat = {};
        if (energy / win < gate) { voiced = false; pending = 0; clarity = 0; anchor = 0; commonFrom = 0; pV = 0; return; }

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
        const double pt = voiced ? period / decim : 0.0;
        // continuidade: se xa había voz, mira primeiro arredor do período anterior (±15 %)
        // para non saltar de oitava no medio dunha nota. Pero se a voz cambiou de nota
        // (o val de agora está lonxe e é claramente máis fondo), o de arredor é un val
        // falso: antes quedaba enganchado nel uns 50 ms e a nota saía unha quinta abaixo
        if (voiced)
        {
            const int lo = std::max (tauMin, (int) std::floor (pt * 0.87)), hi = std::min (tauMax - 1, (int) std::ceil (pt * 1.15));
            int bl = -1;
            for (int t = lo; t <= hi; ++t)
                if (t > 1 && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (bl < 0 || d[(size_t) t] < d[(size_t) bl])) bl = t;
            // un val global a 2–6 veces o período de agora non é outra nota: é un período común
            // (cando aínda soa a nota anterior, na reverberación ou no legato, o val do período
            // común das dúas é o máis fondo). A nota de agora segue mentres teña val de seu
            const bool globalIsMultiple = global >= 0 && multipleOf ((double) global, pt);
            const bool globalClearlyBetter = ! globalIsMultiple && global >= 0 && (global < lo || global > hi) && d[(size_t) global] < d[(size_t) std::max (bl, 0)] - 0.1;
            if (bl >= 0 && d[(size_t) bl] < (globalIsMultiple ? 0.45 : threshold + 0.12) && ! globalClearlyBetter) best = bl;
            // e ao revés: se o primeiro val claro está a un terzo (ou menos) do que se segue,
            // o que se segue é un múltiplo, non a nota (un período común que quedou enganchado)
            if (best >= 0 && global > 0 && std::lround ((double) best / global) >= 3 && multipleOf ((double) best, (double) global)) best = global;
        }
        const bool following = best >= 0;
        bool combed = false, accepted = true;
        if (best < 0) best = global;
        if (best < 0)
        {
            // ningún val por baixo do limiar: o máis fondo, pero só un val de verdade (no
            // bordo do rango, con ruído, a curva adoita seguir baixando e non é un período)
            best = -1;
            for (int t = tauMin + 1; t < tauMax - 1; ++t)
                if (d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (best < 0 || d[(size_t) t] < d[(size_t) best])) best = t;
            // co ruído, o val máis fondo adoita ser un múltiplo longo do período (nunha voz
            // aguda, ata 7 veces): se os múltiplos dun máis curto din outra cousa, vale este
            if (best > 0 && d[(size_t) best] <= 0.35 && ! (voiced && pt > (tauMax - 2) / 2))
            {
                double prom = 0;
                const int cb = combSearch (prom);
                if (cb > 0 && prom >= combNew && best > 1.5 * cb)
                {
                    const double r = (double) best / cb;
                    if (std::abs (r - std::round (r)) < 0.06 * std::round (r)) { best = cb; combed = true; }
                }
            }
            if (best < 0 || d[(size_t) best] > 0.35)
            {
                // voz con moito aire ou ruído: ningún val chega, pero o período ten val en
                // todos os seus múltiplos e o ruído non
                // (só se a nota que se segue cabe dúas veces na xanela: dunha voz grave non
                // se poden mirar os múltiplos, e o que saía era outro período)
                double prom = 0;
                const int cb = voiced && pt > (tauMax - 2) / 2 ? -1 : combSearch (prom);
                int pick = -1;
                // seguindo unha nota, primeiro arredor dela (±15 %), e alí abonda con menos;
                // outro período cun val tan pouco fondo adoita ser outra cousa (a oitava, a
                // guitarra de fondo) e pide o mesmo ca o comezo dunha nota
                if (voiced && cb > 0)
                {
                    const int lo = std::max (tauMin + 1, (int) std::floor (pt * 0.87)), hi = std::min ((tauMax - 2) / 2 - 1, (int) std::ceil (pt * 1.15));
                    for (int t = lo; t <= hi; ++t)
                        if (comb[(size_t) t] <= comb[(size_t) t - 1] && comb[(size_t) t] <= comb[(size_t) t + 1] && (pick < 0 || comb[(size_t) t] < comb[(size_t) pick])) pick = t;
                    if (pick > 0 && combMean - comb[(size_t) pick] < combNear) pick = -1;
                }
                if (pick < 0 && cb > 0 && prom >= combNew) pick = cb;
                if (pick > 0) { best = pick; combed = true; }
            }
            // ningún val claro: o mellor candidato vai igual ao modelo de voz
            if (best < 0 || (d[(size_t) best] > 0.35 && ! combed)) { accepted = false; best = evidence (pt); }
        }
        // ¿hai voz? Un filtro de dous estados (con voz / sen voz) que acumula a evidencia de
        // lectura en lectura: a probabilidade de voz pasa á seguinte coas transicións, e cada
        // lectura multiplícaa pola razón de verosimilitude do modelo (ver voicingLR). Así un
        // val pouco fondo pero constante (voz aireada) segue sendo voz, e unha «s» ou un
        // silencio apágana en dúas lecturas. Entra con 0,6 e sae con 0,4
        {
            if (accepted) { double pr = 0; combSearch (pr); }   // (evidence xa o calculou)
            computeFeatures (best, accepted, pt);
            const double lr = voicingLR (feat);
            const double prior = pV * (1.0 - btE ("BT_A", toUnvoiced)) + (1.0 - pV) * btE ("BT_B", toVoiced);
            pV = lr * prior / (lr * prior + 1.0 - prior);
            // o modelo só sostén unha nota que xa sona: para empezar, as regras de sempre
            const bool keep = voiceModel ? (voiced ? pV > btE ("BT_OUT", 0.4) : accepted && pV > btE ("BT_IN", 0.6)) : accepted;
            if (! keep || best <= 0)
            {
                clarity = best > 0 ? 1.0 - d[(size_t) best] : 0.0;
                voiced = false; pending = 0; anchor = 0; commonFrom = 0; return;
            }
            if (accepted) weakHolds = 0;
            if (! accepted)
            {
                // unha lectura feble pero con voz: o seu período só se arredor da nota que se
                // segue; se non, mantense o de antes (un val feble lonxe adoita ser outra cousa)
                if (feat.x[2] < 0.5)
                {
                    if (++weakHolds > (int) btE ("BT_HOLD", 1000)) { voiced = false; pending = 0; anchor = 0; commonFrom = 0; }
                    return;
                }
                combed = true;   // (o seu período xa vén escollido: sen as regras da oitava)
            }
        }
        // cambio de nota a un múltiplo do período anterior: é o período común da nota que
        // remata (aínda soa: reverberación, legato) e da nova. A nova é un submúltiplo con val
        // de seu que non é período da anterior (dunha cuarta, 3·vella = 4·nova, quedaba dúas
        // oitavas abaixo ata 100 ms). A nota anterior é a da última lectura clara: no paso, o
        // seu val xa se move
        bool common = false;
        const double ref = anchor > 0.0 ? anchor : pt;
        if (best > 0 && ! following && voiced && multipleOf ((double) best, ref))
        {
            const int nw = newNoteIn (best, ref);
            // a nova aínda non ten val de seu: mantense a anterior uns ms (no canto do común)
            if (nw <= 0 && holds < maxHolds) { ++holds; return; }
            // e se non aparece, quédase o longo (pode ser de verdade unha oitava abaixo). En
            // calquera caso lémbrase a nota de antes uns 0,4 s, mentres a súa cola soa
            if (nw > 0) { best = nw; common = true; }
            commonFrom = ref; commonAge = 0;
        }
        else if (commonFrom > 0.0 && ++commonAge > 100) commonFrom = 0;   // ~0,4 s: a cola xa non conta
        holds = 0;
        // A oitava: entre o val elixido e os da súa familia (a metade, o dobre, o cuádruplo
        // do período), quédase o máis curto que sexa case tan fondo coma o máis fondo. Así
        // non se queda unha oitava arriba cando o fundamental é feble (unha vogal co primeiro
        // formante no segundo harmónico: a voz é case periódica a medio período), nin unha
        // abaixo (o val ao dobre do período é sempre case tan fondo coma o verdadeiro). E,
        // como se mira en cada lectura, un erro solto non queda enganchado pola continuidade.
        const int pre = best;
        if (best > 0 && ! common && ! combed) best = following ? octaveSwitch (best) : octaveFamily (best);
        if (std::getenv ("BT_TR")) { const double tn = (double) pos * decim / sr; if (tn >= std::atof (std::getenv ("BT_TR")) && tn < std::atof (std::getenv ("BT_TR")) + 0.08)
            std::fprintf (stderr, "T %.3f v=%d prevHz %.0f glob %d acc %d foll %d comb %d pre %d -> %d (%.0f Hz) d=%.3f pV %.3f x=[%.2f %.2f %.0f %.2f %.2f]\n", tn, (int) voiced, voiced ? dsr / pt : 0.0, global, (int) accepted, (int) following, (int) combed, pre, best, dsr / best, d[(size_t) best], pV, feat.x[0], feat.x[1], feat.x[2], feat.x[4], feat.x[5]); }
        double tau = best;
        if (best > 1 && best < tauMax)
        {
            const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
            const double den = a - 2 * b + c;
            // (±0,5 como moito: o candidato dos múltiplos pode non ser un val de d, e nunha
            // costa case recta a parábola daba calquera cousa, ata períodos negativos)
            if (den > 0) tau += clampd (0.5 * (a - c) / den, -0.5, 0.5);
        }
        // un período fóra do rango non é voz (e nunca pode saír de aquí)
        if (! (tau >= tauMin - 1 && tau <= tauMax + 1)) { voiced = false; pending = 0; anchor = 0; commonFrom = 0; return; }
        clarity = 1.0 - d[(size_t) best];
        anchor = d[(size_t) best] < 0.15 ? tau : anchor;
        const double p = tau * decim;
        if (! voiced)
        {
            // comezo de nota: fai falta que dúas lecturas seguidas coincidan (±0,6 semitonos)
            if (pending > 0 && std::abs (std::log2 (p / pending)) < 0.05) { period = p; voiced = true; }
            pending = p;
            return;
        }
        period = p;
    }

    // Razón de verosimilitude voz / sen voz dunha lectura: regresión loxística sobre as
    // características (tools/DetectorTest.cpp --features volca os datos e
    // tools/axustar_voz.py axusta os pesos), dividida polas probabilidades a priori dos datos
    static double voicingLR (const Features& f)
    {
        const double* x = f.x;
        const double t[voiceTerms] = { x[0], x[1], x[2], x[4], x[5], x[6],
                                       x[0] * x[0], x[1] * x[1], x[0] * x[1], x[2] * x[1] };
        double z = voiceW[voiceTerms];
        for (int i = 0; i < voiceTerms; ++i) z += voiceW[i] * t[i];
        const double odds = std::exp (clampd (z, -30.0, 30.0)) / (voicePrior / (1.0 - voicePrior));
        return clampd (odds, 1e-4, 1e4);
    }

    // seguindo unha nota (continuidade), a oitava só cambia con probas claras. (A regla
    // da familia enteira, en cada lectura, facía saltos soltos nas voces graves reais.)
    int octaveSwitch (int best) const
    {
        const double here = fracDepth (best);
        for (double mlt : { 2.0, 0.5 })
        {
            const double t = best * mlt;
            // o dobre non pode ser o período común coa nota de antes (aínda soa a súa cola)
            if (mlt > 1.0 && commonFrom > 0.0 && multipleOf (t, commonFrom)) continue;
            // e, mentres soa a cola, non se vai ao dobre se o val de agora xa é periódico de seu
            if (mlt > 1.0 && commonFrom > 0.0 && here < threshold) continue;
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

    // o mellor candidato aínda que non chegue: arredor da nota que se segue (o da media dos
    // múltiplos ou o val), e se non, o dos múltiplos ou o val máis fondo
    int evidence (double pt)
    {
        double prom = 0;
        const int cb = combSearch (prom);
        const int tEnd = (tauMax - 2) / 2;
        if (pt > 0.0)
        {
            const int lo = std::max (tauMin + 1, (int) std::floor (pt * 0.87)), hi = std::min (tauMax - 2, (int) std::ceil (pt * 1.15));
            int bc = -1, bd = -1;
            for (int t = lo; t <= hi; ++t)
            {
                if (t < tEnd && comb[(size_t) t] <= comb[(size_t) t - 1] && comb[(size_t) t] <= comb[(size_t) t + 1] && (bc < 0 || comb[(size_t) t] < comb[(size_t) bc])) bc = t;
                if (d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (bd < 0 || d[(size_t) t] < d[(size_t) bd])) bd = t;
            }
            // (primeiro o val de d: o seu período afínase coa parábola; o mínimo da media dos
            // múltiplos é un atraso enteiro, e nunha voz aguda unha mostra é un semitono)
            if (bd > 0) return bd;
            if (bc > 0) return bc;
        }
        if (cb > 0) return cb;
        int best = -1;
        for (int t = tauMin + 1; t < tauMax - 1; ++t)
            if (d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1] && (best < 0 || d[(size_t) t] < d[(size_t) best])) best = t;
        return best;
    }

    // (comb e combMean xa calculados nesta lectura)
    void computeFeatures (int c, bool accepted, double pt)
    {
        const int tEnd = (tauMax - 2) / 2;
        feat.cand = c; feat.accepted = accepted; feat.valid = true;
        auto dmin3 = [this] (int k) { return std::min ({ d[(size_t) k - 1], d[(size_t) k], d[(size_t) k + 1] }); };
        const bool ok = c > 1 && c < tauMax - 1;
        feat.x[0] = ok ? clampd (fracDepth (c), 0.0, 1.5) : 1.5;
        feat.x[1] = ok ? clampd (combMean - (c < tEnd ? comb[(size_t) c] : dmin3 (c)), -0.5, 1.0) : -0.5;
        feat.x[2] = ok && pt > 0.0 && std::abs (std::log2 (c / pt)) < 0.1 ? 1.0 : 0.0;
        feat.x[3] = clampd (10.0 * std::log10 ((frameEnergy + 1e-20) / (peakE + 1e-20)), -60.0, 0.0) / 60.0;
        feat.x[4] = clampd (std::log10 ((hfEnv + 1e-20) / (lfEnv + 1e-20)), -4.0, 4.0) / 4.0;
        feat.x[5] = clampd (combMean, 0.0, 2.0);
        feat.x[6] = accepted ? 1.0 : 0.0;
    }

    // Con moito aire ou ruído o val do período non baixa do limiar, pero aparece en todos
    // os seus múltiplos (P, 2P, 3P, 4P) e o ruído non. A media deles (cada un ±1 mostra: o
    // múltiplo cae entre mostras) ten que quedar claramente por baixo do resto da curva.
    // Entre os case iguais, o máis curto (os múltiplos de P tamén teñen os seus múltiplos)
    int combSearch (double& prominence)
    {
        prominence = 0;
        double mean = 0;
        for (int t = tauMin; t < tauMax; ++t) mean += d[(size_t) t];
        mean /= std::max (1, tauMax - tauMin);
        combMean = mean;
        double smin = 1e9;
        const int tEnd = (tauMax - 2) / 2;
        for (int t = tauMin; t <= tEnd; ++t)
        {
            double sum = 0; int k = 0;
            for (int m = 1; m <= 4 && m * t + 1 < tauMax; ++m, ++k)
            {
                const int c = m * t;
                sum += std::min ({ d[(size_t) c - 1], d[(size_t) c], d[(size_t) c + 1] });
            }
            comb[(size_t) t] = sum / k;
            smin = std::min (smin, comb[(size_t) t]);
        }
        if (smin > 0.65) return -1;
        auto dmin3 = [this] (int c) { return std::min ({ d[(size_t) c - 1], d[(size_t) c], d[(size_t) c + 1] }); };
        for (int t = tauMin + 1; t < tEnd; ++t)
            if (comb[(size_t) t] <= smin + 0.05 && comb[(size_t) t] <= comb[(size_t) t - 1] && comb[(size_t) t] <= comb[(size_t) t + 1])
            {
                // se o val ao dobre é claramente máis fondo ca o seu, t é medio período (unha
                // voz grave co segundo harmónico forte): o de verdade non se pode mirar así
                if (dmin3 (2 * t) < dmin3 (t) - 0.15) return -1;
                // a familia: un período que o divide (2–5 veces) e cuxa media é case tan baixa
                // é o de verdade (os atrasos longos teñen menos múltiplos e a normalización
                // fainos máis fondos; unha voz aguda e aireada quedaba dúas oitavas abaixo).
                // A metade dun período non pasa: os seus múltiplos impares son lombas
                int pick = t;
                for (int k = 5; k >= 2; --k)
                {
                    const int s0 = (int) std::lround ((double) t / k);
                    if (s0 < tauMin + 1) continue;
                    int sk = s0;
                    for (int j = s0 - 1; j <= s0 + 1; ++j) if (comb[(size_t) j] < comb[(size_t) sk]) sk = j;
                    if (comb[(size_t) sk] <= comb[(size_t) t] + btE ("BT_FAM", 0.15) && comb[(size_t) sk] <= comb[(size_t) sk - 1] && comb[(size_t) sk] <= comb[(size_t) sk + 1]) { pick = sk; break; }
                }
                prominence = mean - comb[(size_t) pick];
                return pick;
            }
        return -1;
    }

    // Dentro dun período común «tc» (múltiplo de «old», a nota que remata), a nota nova: o
    // submúltiplo máis curto con val de seu (case tan fondo coma o máis fondo deles) que non
    // sexa período da vella. 0 se aínda non hai
    int newNoteIn (int tc, double old) const
    {
        int cand[6]; double dep[6]; int nc = 0; double dmin = 1e9;
        for (int k = 6; k >= 2; --k)   // de curto a longo
        {
            const double t = (double) tc / k;
            if (t < tauMin + 1 || multipleOf (t, old) || std::abs (t / old - 1.0) < 0.04) continue;
            int ta = 0;
            if (! dipAround (t, ta)) continue;
            const double dp = fracDepth (ta);
            if (dp < 0.45) { cand[nc] = ta; dep[nc] = dp; ++nc; dmin = std::min (dmin, dp); }
        }
        for (int i = 0; i < nc; ++i)
            if (dep[i] <= 2.0 * dmin + 0.05) return cand[i];
        return 0;
    }

    // ¿t é 2–6 veces u (±4 %)?
    static bool multipleOf (double t, double u)
    {
        if (u <= 0.0) return false;
        const double r = t / u;
        const int k = (int) std::lround (r);
        return k >= 2 && k <= 6 && std::abs (r - k) < 0.04 * k;
    }

    // o val máis fondo arredor de t (±4 %), se é un val de verdade e non o bordo dunha costa
    bool dipAround (double t, int& at) const
    {
        const int lo = std::max (tauMin, (int) std::floor (t * 0.96) - 1), hi = std::min (tauMax - 1, (int) std::ceil (t * 1.04) + 1);
        if (lo > hi) return false;
        at = lo;
        for (int k = lo; k <= hi; ++k) if (d[(size_t) k] < d[(size_t) at]) at = k;
        return at > lo && at < hi;
    }

    int octaveFamily (int best) const
    {
        int cand[8]; double dep[8]; int nc = 0;
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
    std::vector<double> d, cum, comb;
    double combMean = 1.0;
    // canto por baixo do resto da curva ten que quedar a media dos múltiplos: arredor da
    // nota que se segue, e para unha nota nova (voz aireada: erros 8,2 -> 2,6 %, tramas
    // perdidas 16 -> 11 %; máis baixo, a guitarra de fondo e as voces graves daban notas)
    static constexpr double combNear = 0.4, combNew = 0.5;
    int64_t frameStart = 0;
    Biquad lp1, lp2, lfBand;
    double hfA = 0.5, hfX = 0, hfY = 0, hfEnv = 0, lfEnv = 0, envA = 0.005, peakE = 0, peakDecay = 0.98, frameEnergy = 0;
    double hp = 0, hpPrev = 0;
    int64_t pos = 0;
    int dcount = 0, hcount = 0;
    double pending = 0;
    double pV = 0;   // probabilidade de voz (filtro de dous estados)
    int weakHolds = 0;
    static constexpr double toVoiced = 0.02, toUnvoiced = 0.02;   // transicións por lectura (4 ms)
    static constexpr int voiceTerms = 10;
    static constexpr double voicePrior = 0.889043;
    static constexpr double voiceW[voiceTerms + 1] = { -4.279169, 3.994957, 1.259783, -12.142956, -9.945671, 2.286550, -1.952468, 3.009986, -7.301386, 0.627467, 13.999692 };
    double anchor = 0;                 // período (diezmado) da última lectura clara
    int holds = 0;
    static constexpr int maxHolds = 6; // ~24 ms
    double commonFrom = 0;             // seguindo un período común: a nota de antes
    int commonAge = 0;
};

} // namespace tune
