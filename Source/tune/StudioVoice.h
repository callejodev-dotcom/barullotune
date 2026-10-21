// Barullo Tune — voz de saída de Estudio: TD-PSOLA en tempo real.
//
// Cada grano lese arredor dunha marca de ciclo e corríxese co seu propio período; os
// formantes móvense lendo o grano a outra velocidade. Varias voces de Estudio poden ler
// da mesma análise (para as armonías).
#pragma once
#include "Analysis.h"

namespace tune {

class StudioVoice
{
public:
    StudioVoice (const Settings& s, Analysis& a) : set (s), an (a), corr (s), decider (s, a.midi) {}

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        corr.prepare (sr);
        decider.prepare (sr);
        for (int c = 0; c < 2; ++c) outCh[c].assign ((size_t) an.ringSize, 0.0);
    }

    static int latencyFor (int pMax) { return (int) std::ceil (2.5 * pMax) + 32; }

    void reset (int latencySamples)
    {
        for (int c = 0; c < 2; ++c) std::fill (outCh[c].begin(), outCh[c].end(), 0.0);
        latency = latencySamples;
        nextSyn = (double) an.pMax; srcOff = -(double) latency; synVoiced = false; state = {};
        decider.reset(); lastDecPos = -1e18;
    }

    void process (float* y)
    {
        const int64_t n = an.n;
        while (nextSyn <= (double) (n + an.pMax)) synthesize();
        const size_t o = (size_t) (n & (an.ringSize - 1));
        for (int c = 0; c < an.nch; ++c)
        {
            y[c] = (float) outCh[c][o];   // PSOLA clásico: sen normalizar (cada pulso conserva o seu nivel)
            outCh[c][o] = 0;
        }
    }

private:
    // A nota de destino de cada grano decídese co ton arredor del (os ciclos a ±10 ms):
    // Estudio le a voz 15–42 ms atrasada, así xa os ten medidos e a nota cambia onde
    // cambia a voz, sen o atraso do detector. En MIDI nota manda a tecla (a da marca).
    int decideTarget (int k, const Analysis::Mark& mk)
    {
        if (! mk.voiced || mk.target < 0) { decider.reset(); return mk.target; }
        if (set.mode == Settings::MidiNote && an.midi.lastNote >= 0) return mk.target;
        if (decider.active && mk.pos <= lastDecPos) return decider.target;   // o mesmo grano outra vez
        const double win = 0.01 * sr;
        double acc = 0; int cnt = 0;
        for (int j = k; j >= 0 && an.markAt (j).voiced && mk.pos - an.markAt (j).pos <= win; --j) { acc += midiOf (an.localPeriod (j)); ++cnt; }
        for (int j = k + 1; j + 1 < an.nMarks && an.markAt (j).voiced && an.markAt (j).pos - mk.pos <= win; ++j) { acc += midiOf (an.localPeriod (j)); ++cnt; }
        const double mDec = acc / std::max (1, cnt);
        const double dt = decider.active ? mk.pos - lastDecPos : 0.0;
        lastDecPos = mk.pos;
        return decider.update (mDec, dt);
    }

    double midiOf (double period) const { return 69.0 + 12.0 * std::log2 (sr / period / set.refA); }

    void synthesize()
    {
        const int64_t n = an.n;
        const double ti = nextSyn - latency;
        const double fr = std::pow (2.0, clampd (set.formant, -7.0, 7.0) / 12.0);   // lectura máis rápida = formantes máis altos
        const double reach = std::max (1.0, fr);
        auto ready = [&] (const Analysis::Mark& m2) { return m2.pos + std::ceil (m2.period * (m2.voiced ? reach : 1.0)) + 2 + Sinc::half <= (double) n; };
        // marca máis próxima a ti que xa teña todo o grano dispoñible
        int bestK = -1; double bd = 1e18;
        for (int k = 0; k < an.nMarks; ++k)
        {
            const auto& mk = an.markAt (k);
            if (! ready (mk)) continue;
            const double dd = std::abs (mk.pos - ti);
            if (dd < bd) { bd = dd; bestK = k; }
        }
        if (bestK < 0) { nextSyn += an.unvoicedP; return; }
        if (an.markAt (bestK).voiced && ! synVoiced)
        {
            // comezo dunha zona con voz: escolle a marca máis próxima a onde se estaba lendo
            // e move un pouco o grano de saída para que case sen salto co anterior
            const double want = nextSyn + srcOff;
            double bd2 = 1e18;
            for (int k = 0; k < an.nMarks; ++k)
            {
                const auto& m2 = an.markAt (k);
                if (! m2.voiced || ! ready (m2)) continue;
                const double dd = std::abs (m2.pos - want);
                if (dd < bd2) { bd2 = dd; bestK = k; }
            }
            const double ns = an.markAt (bestK).pos - srcOff;
            if (ns > (double) n + 1.0) nextSyn = ns;
        }
        const Analysis::Mark mk = an.markAt (bestK);
        const int target = decideTarget (bestK, mk);

        double P = mk.period, spacing = P, rf = 1.0;
        if (mk.voiced && target >= 0)
        {
            // cada grano co seu propio ton: a corrección calcúlase co período deste grano,
            // así a saída cae xusto na nota aínda que a voz se estea movendo
            P = an.localPeriod (bestK);
            const double m = 69.0 + 12.0 * std::log2 (sr / P / set.refA);
            double om = m;
            const double cc = corr.step (state, m, target, P, an.isFree (target), an.bendNow(), om);
            synVoiced = true;
            spacing = P / std::pow (2.0, cc / 12.0);
            an.correctionCents = (cc - set.transpose) * 100.0;
            an.outputMidi = om;
            rf = fr;
        }
        else
        {
            // xordo: OLA sen cambio de tempo
            P = an.unvoicedP; spacing = P;
            synVoiced = false; state.active = false; an.correctionCents = 0;
        }
        const int half = (int) std::ceil (P);
        const int64_t c = (int64_t) std::floor (nextSyn);
        const double frac = nextSyn - (double) c;
        double src;   // centro de lectura
        if (mk.voiced && synVoiced) src = mk.pos;
        else
        {
            // xordo: lectura continua desde onde quedou, achegándose amodo ao atraso nominal
            const double nominal = -(double) latency;
            const double step = P * 0.08;
            srcOff += clampd (nominal - srcOff, -step, step);
            src = nextSyn + srcOff;
        }
        if (mk.voiced && synVoiced) srcOff = mk.pos - nextSyn;
        const int nch = an.nch, ringSize = an.ringSize;
        for (int k = -half; k <= half; ++k)
        {
            const double off = k - frac;          // distancia ao centro do grano (na saída)
            if (std::abs (off) >= P) continue;
            if (c + k < n) continue;
            const double w = 0.5 * (1.0 + std::cos (kPi * off / P));
            const double sp = src + off * rf;
            const int64_t i0 = (int64_t) std::floor (sp);
            if (i0 + Sinc::half > n || i0 < n - ringSize + 2 * Sinc::half) continue;
            for (int ch = 0; ch < nch; ++ch) outCh[ch][(size_t) ((c + k) & (ringSize - 1))] += an.read (an.inCh[ch], sp) * w;
        }
        nextSyn += spacing;
    }

    const Settings& set;
    Analysis& an;
    Corrector corr;
    CorrState state;
    NoteDecider decider;
    double lastDecPos = -1e18;
    std::vector<double> outCh[2];
    double sr = 48000;
    int latency = 64;
    double nextSyn = 0, srcOff = 0;
    bool synVoiced = false;
};

} // namespace tune
