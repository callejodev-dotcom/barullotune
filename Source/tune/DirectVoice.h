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
    DirectVoice (const Settings& s, Analysis& a, VoiceRole r = {}) : set (s), an (a), role (r), corr (s), decider (s, a.midi) {}

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
        ffixAlphaFast = 1.0 - std::exp (-1.0 / (0.005 * sr));
        envAlpha = 1.0 - std::exp (-1.0 / (0.003 * sr));
        gateAlpha = 1.0 - std::exp (-1.0 / (0.01 * sr));
        dirAlpha = 1.0 - std::exp (-1.0 / (0.0015 * sr));
        lookAhead = 0.0015 * sr;   // o atraso dese suavizado
    }

    // preciso: a lectura vai 11,5 ms detrás, dentro de ciclos xa medidos (sen extrapolar)
    void configure (bool isPrecise) { precise = isPrecise; dLead = precise ? dMin + (int) std::lround (0.0115 * sr) : dMin; }
    int latency() const { return dLead; }

    void reset() { restart (0); }

    // empeza a ler no punto n (a análise segue coma estaba)
    void restart (int64_t n)
    {
        ffix.reset();
        envIn[0] = envIn[1] = envOut[0] = envOut[1] = 0;
        decider.reset(); decCount = 0;
        rp = (double) n - (double) dLead; rp2 = 0; xfPos = -1; dirCorr = 0; rho = 1.0; state = {};
        gate = 0;
    }

    void process (float* y)
    {
        // a principal le todas as canles; unha armonía, o centro (mono)
        const int nch = role.harmony ? 1 : an.nch;
        auto src = [this] (int c) -> const std::vector<float>& { return role.harmony ? an.in : an.inCh[c]; };
        const bool voiced = an.voiced;
        const double transpose = set.transpose;
        // corrección mostra a mostra (co ton detectado agora mesmo). Sen ton (consoantes
        // xordas, respiracións) lese a velocidade 1: unha «s» non ten nota que transpor, e
        // transposta e despois corrixida polos formantes saía ata 5 dB máis forte
        double raw = voiced ? transpose : 0.0;
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
        // armonía: a mesma corrección cá principal e, enriba, o intervalo ata a súa nota
        double offset = 0.0, formant = set.formant;
        bool sounding = voiced && target >= 0;
        if (role.harmony)
        {
            const int ht = sounding ? harmonyNote (set, an.midi, target, role.index) : -1;
            sounding = ht >= 0;
            if (sounding) offset = (ht - target) + set.harm[role.index].detune * 0.01;
            formant += set.harm[role.index].formant;
        }
        const bool starting = sounding && ! state.active;
        if (voiced && target >= 0) { double om; raw = corr.step (state, mNow, target, 1.0, an.isFree (target), an.bendNow(), om) + offset; }
        else state.active = false;
        if (starting) dirCorr = raw;
        else dirCorr += (raw - dirCorr) * dirAlpha;   // 1,5 ms de suavizado, para que non rasque
        rho = std::pow (2.0, dirCorr / 12.0);
        if (! role.harmony)
        {
            an.correctionCents = voiced ? (dirCorr - transpose) * 100.0 : 0.0;
            if (voiced) an.outputMidi = mNow + dirCorr;
        }

        // saída: un punteiro (ou dous durante un fundido), o mesmo para cada canle
        for (int c = 0; c < nch; ++c) y[c] = an.read (src (c), rp);
        if (xfPos >= 0)
        {
            const double w = 0.5 - 0.5 * std::cos (kPi * (xfPos + 0.5) / xfLen);
            for (int c = 0; c < nch; ++c) y[c] = (float) (y[c] * (1.0 - w) + an.read (src (c), rp2) * w);
            rp2 += rho;
            if (++xfPos >= xfLen) { rp = rp2; xfPos = -1; }
        }
        rp += rho;

        // formantes: ao transpor (ou co mando de formantes) devolve o timbre da voz orixinal.
        // Sen ton (consoantes, respiracións) lese a velocidade 1 e non hai nada que corrixir:
        // apágase en 5 ms (na fronteira entre unha vogal e unha «s», as dúas envolventes son
        // de sons distintos e unha resonancia aguda disparábase uns 10 ms)
        const bool want = voiced && (std::abs (transpose + offset) >= 0.99 || std::abs (formant) >= 0.05);
        ffix.amount += ((want ? 1.0 : 0.0) - ffix.amount) * (want ? ffixAlpha : ffixAlphaFast);
        if (ffix.amount < 1e-4 && ! want) { if (ffix.ready) ffix.reset(); }
        else
        {
            const float mid = nch == 1 ? y[0] : 0.5f * (y[0] + y[1]);
            if (ffix.pushY (mid))
            {
                // marco da orixinal, lido á velocidade dos formantes (máis rápido = formantes
                // máis altos) e aliñado co que se está lendo: remata no punto de lectura, non
                // na última mostra que entrou (a lectura vai ata 15 ms detrás e, entre unha
                // vogal e unha «s», as dúas envolventes eran de sons distintos)
                const double phi = std::pow (2.0, clampd (formant, -7.0, 7.0) / 12.0);
                const double start = std::min (rp, (double) an.n - Sinc::half) - (double) ffix.frame * phi;
                for (int i = 0; i < ffix.frame; ++i) ffix.xf[(size_t) i] = an.read (an.in, start + i * phi);
                ffix.analyse (ffix.xf.data());   // (analiza a orixinal antes de reutilizar o marco)
            }
            for (int c = 0; c < nch; ++c)
            {
                double fixed = ffix.process (c, y[c]);
                // seguridade: a corrección non pode deixar a voz máis do dobre de forte (3 ms)
                envIn[c] += ((double) y[c] * y[c] - envIn[c]) * envAlpha;
                envOut[c] += (fixed * fixed - envOut[c]) * envAlpha;
                if (envOut[c] > 4.0 * envIn[c] + 1e-12) fixed *= std::sqrt ((4.0 * envIn[c] + 1e-12) / envOut[c]);
                y[c] = (float) (y[c] + (fixed - y[c]) * ffix.amount);
            }
        }
        // unha armonía só soa cando hai voz e nota (sen ton, as consoantes dobradas emborranan)
        if (role.harmony)
        {
            gate += ((sounding ? 1.0 : 0.0) - gate) * gateAlpha;
            y[0] = (float) (y[0] * gate);
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
    VoiceRole role;
    Corrector corr;
    CorrState state;
    NoteDecider decider;
    double gate = 0, gateAlpha = 0.002;
    int decHop = 192, decCount = 0;
    bool precise = false;
    FormantFix ffix;
    double sr = 48000;
    double rp = 0, rp2 = 0, dirCorr = 0, rho = 1.0, dirAlpha = 0.01, lookAhead = 0, ffixAlpha = 0.001, ffixAlphaFast = 0.004, envAlpha = 0.007;
    double envIn[2] {}, envOut[2] {};
    int xfade = 36, dMin = 44, dLead = 44;
    int xfPos = -1, xfLen = 36;
    double score[maxSearch] {};
};

} // namespace tune
