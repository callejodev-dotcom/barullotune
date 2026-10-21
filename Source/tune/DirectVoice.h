// Barullo Tune — voz de saída de Directo e Preciso.
//
// A voz lese a outra velocidade e, cando o punteiro de lectura se adianta ou se atrasa,
// salta exactamente un ciclo cun fundido curto (o método do afinador clásico). O ton no
// punto que se le sae do ton deslizante; ao transpor ou mover os formantes, a corrección
// de formantes sen latencia devolve o timbre da voz orixinal.
#pragma once
#include "Analysis.h"
#include "FormantFix.h"

namespace tune {

class DirectVoice
{
public:
    DirectVoice (const Settings& s, Analysis& a) : set (s), an (a), corr (s), decider (s, a.midi) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        corr.prepare (sr);
        decider.prepare (sr);
        decHop = std::max (1, (int) std::lround (0.004 * sr));
        xfade = std::max (16, (int) std::lround (sr * 0.0006));
        dMin = (int) std::ceil (1.1 * xfade) + 4 + Sinc::half;   // o interpolador mira 8 mostras adiante
        ffix.prepare (sr);
        ffixAlpha = 1.0 - std::exp (-1.0 / (0.02 * sr));
        dirAlpha = 1.0 - std::exp (-1.0 / (0.0015 * sr));
        lookAhead = 0.0015 * sr;   // o atraso dese suavizado
    }

    // preciso: a lectura vai 11,5 ms detrás, dentro de ciclos xa medidos (sen extrapolar)
    void configure (bool isPrecise) { precise = isPrecise; dLead = precise ? dMin + (int) std::lround (0.0115 * sr) : dMin; }
    int latency() const { return dLead; }

    void reset()
    {
        ffix.reset();
        decider.reset(); decCount = 0;
        rp = -(double) dLead; rp2 = 0; xfPos = -1; dirCorr = 0; rho = 1.0; state = {};
    }

    void process (float* y)
    {
        const int nch = an.nch;
        const bool voiced = an.voiced;
        const double transpose = set.transpose;
        // corrección mostra a mostra (co ton detectado agora mesmo)
        double raw = transpose;
        double mNow = an.inputMidi;
        if (voiced)
        {
            double mi = 0;
            // o suavizado da corrección (1,5 ms) atrasaría a corrección: o ton mírase ese
            // anaco por diante do punto de lectura, así a corrección chega a tempo
            if (an.pitchAtSliding (rp + rho * lookAhead, mi)) mNow = mi;   // o ton medido á par da lectura
            else if (an.pitchAtFromMarks (rp, mi)) mNow = mi;            // xa medido: o ton dos ciclos onde se le
            else if (an.cycleOk)
                mNow = an.cycleM + an.cycleSlope * clampd (rp - an.cycleT, 0.0, 4.0 * an.curP);   // aínda non: extrapólase
        }
        // a nota de destino decídese co ton do punto que se le (sen o atraso do detector);
        // en Preciso, co de 6 ms máis adiante (xa se coñece): así cambia xusto onde cambia a voz
        int target = an.target;
        if (voiced && target >= 0)
        {
            double mDec = mNow;
            if (precise) { double mi = 0; if (an.pitchAtSliding (rp + 0.5 * ((double) an.n - rp), mi)) mDec = mi; }
            if (! decider.active) { decider.update (mDec, 0.0); decCount = 0; }
            else if (++decCount >= decHop) { decider.update (mDec, (double) decCount); decCount = 0; }
            target = decider.target;
        }
        else if (decider.active) decider.reset();
        const bool starting = voiced && target >= 0 && ! state.active;
        if (voiced && target >= 0) { double om; raw = corr.step (state, mNow, target, 1.0, an.isFree (target), an.bendNow(), om); }
        else state.active = false;
        if (starting) dirCorr = raw;
        else dirCorr += (raw - dirCorr) * dirAlpha;   // 1,5 ms de suavizado, para que non rasque
        rho = std::pow (2.0, dirCorr / 12.0);
        an.correctionCents = voiced ? (dirCorr - transpose) * 100.0 : 0.0;
        if (voiced) an.outputMidi = mNow + dirCorr;

        // saída: un punteiro (ou dous durante un fundido), o mesmo para cada canle
        for (int c = 0; c < nch; ++c) y[c] = an.read (an.inCh[c], rp);
        if (xfPos >= 0)
        {
            const double w = 0.5 - 0.5 * std::cos (kPi * (xfPos + 0.5) / xfLen);
            for (int c = 0; c < nch; ++c) y[c] = (float) (y[c] * (1.0 - w) + an.read (an.inCh[c], rp2) * w);
            rp2 += rho;
            if (++xfPos >= xfLen) { rp = rp2; xfPos = -1; }
        }
        rp += rho;

        // formantes: ao transpor (ou co mando de formantes) devolve o timbre da voz orixinal
        const double formant = set.formant;
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
                const int64_t n = an.n;
                if (std::abs (formant) < 0.05)
                    for (int i = 0; i < ffix.frame; ++i) ffix.xf[(size_t) i] = an.in[(size_t) ((n - ffix.frame + 1 + i) & (an.ringSize - 1))];
                else
                {
                    const double start = (double) n - (double) ffix.frame * phi;
                    for (int i = 0; i < ffix.frame; ++i) ffix.xf[(size_t) i] = an.read (an.in, start + i * phi);
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
            const double D = (double) an.n - rp;     // atraso actual
            const double P = voiced ? an.curP : 2.0 * an.unvoicedP;
            // o salto escóllese comparando medio ciclo (o fundido é curto: así non mestura dous ciclos distintos)
            const int cmp = (int) clampd (P * 0.5, (double) xfade, 2048.0);
            xfLen = xfade;
            if (rho > 1.0 && D < (double) dLead)
            {
                // a lectura alcanza a escritura (ton para arriba): volve atrás un ciclo
                const double j = voiced ? bestJump (rp, P, P * 0.12, -1.0, (double) (an.ringSize / 2), std::min (cmp, (int) D - 2)) : P;
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

private:
    // Mellor salto (arredor de nominal, en ±range) para que o tramo que empeza en p+sign·j
    // se pareza ao que empeza en p: correlación normalizada sobre o fundido + parábola.
    double bestJump (double p, double nominal, double spread, double sign, double maxJump, int len)
    {
        const int lo = std::max (1, (int) std::floor (nominal - spread));
        const int hi = std::min ({ (int) std::floor (maxJump), (int) std::ceil (nominal + spread), lo + maxSearch - 1 });
        if (hi < lo) return std::max (1.0, std::min (nominal, maxJump));
        const int64_t base = (int64_t) std::floor (p);
        auto X = [this] (int64_t i) { return (double) an.in[(size_t) (i & (an.ringSize - 1))]; };
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

    static constexpr int maxSearch = Analysis::maxSearch;
    const Settings& set;
    Analysis& an;
    Corrector corr;
    CorrState state;
    NoteDecider decider;
    int decHop = 192, decCount = 0;
    bool precise = false;
    FormantFix ffix;
    double sr = 48000;
    double rp = 0, rp2 = 0, dirCorr = 0, rho = 1.0, dirAlpha = 0.01, lookAhead = 0, ffixAlpha = 0.001;
    int xfade = 36, dMin = 44, dLead = 44;
    int xfPos = -1, xfLen = 36;
    double score[maxSearch] {};
};

} // namespace tune
