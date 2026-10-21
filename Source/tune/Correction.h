// Barullo Tune — axustes da corrección e o modelo de corrección (común a todos os xeitos).
#pragma once
#include "Dsp.h"

namespace tune {

// parámetros (pódense cambiar en calquera momento)
struct Settings
{
    enum Mode { ScaleMode = 0, MidiScale = 1, MidiNote = 2 };

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
    double bendRange = 2.0;    // MIDI nota: a roda de pitch bend dobra a nota ±isto
    double formant = 0.0;      // semitonos (en Directo e Preciso, coa corrección de formantes)
};

// ---- o modelo de corrección (o mesmo para todos os xeitos) ----
// O vibrato sácase cun filtro de banda (3–8 Hz) sobre o ton; o resto é o «centro» da voz.
// O centro achégase á nota con «velocidade»; a nota de destino móvese dunha a outra
// con «transición»; o vibrato volve sumarse multiplicado por «vibrato».
struct CorrState
{
    bool active = false; int lastTarget = -1;
    double tS = 0, cS = 0, vibRamp = 0, tolTimer = 0;
    double ic1 = 0, ic2 = 0;   // filtro de estado (TPT) do vibrato
    double prevCentre = 0, slope = 0;   // pendente do centro da voz (semitonos/s)
    // detector de vibrato: só se respecta unha oscilación regular (4–8 Hz, varios ciclos)
    double clock = 0, lastZc = -1e9, lastHalf = 0, vibEnv = 0, vibConf = 0; int vibSign = 0;
};

class Corrector
{
public:
    explicit Corrector (const Settings& s) : set (s) {}
    void prepare (double sampleRate) { sr = sampleRate; }

    double alphaFor (double ms, double dt) const { return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-dt / (ms * 0.001 * sr)); }

    // corrección en semitonos para unha voz en m (MIDI) que vai a tgt; dt = mostras desde o
    // paso anterior. free: a nota é libre (non se corrixe); bend: pitch bend en semitonos
    double step (CorrState& st, double m, int tgt, double dt, bool free, double bend, double& outMidi) const
    {
        const double transpose = set.transpose;
        if (! st.active)
        {
            // comezo dunha nota: xa afinada
            st.active = true; st.tS = tgt; st.vibRamp = 0; st.tolTimer = 0;
            st.ic1 = 0; st.ic2 = m;
            st.prevCentre = m; st.slope = 0;
            st.clock = 0; st.lastZc = -1e9; st.lastHalf = 0; st.vibEnv = 0; st.vibConf = 0; st.vibSign = 0;
            const double err = st.tS - m;
            st.cS = (free || std::abs (err) * 100.0 < set.tolCents) ? 0.0 : err * set.amount;
            st.lastTarget = tgt;
            outMidi = m + st.cS + transpose + bend;
            return st.cS + transpose + bend;
        }
        if (tgt != st.lastTarget) st.vibRamp = 0;   // nota nova: o vibrato volve entrar amodo
        st.lastTarget = tgt;
        st.tS += (tgt - st.tS) * alphaFor (set.transitionMs, dt);
        st.vibRamp = std::min (1.0, st.vibRamp + dt / (0.10 * sr));
        const double band = clampd (vibratoBand (st, m, dt), -1.5, 1.5);
        updateVibratoConfidence (st, band, dt);
        const double vib = band * st.vibRamp * st.vibConf;
        const double centre = m - vib;
        const double err = st.tS - centre;
        // portamentos: mentres a voz vai dunha nota a outra (máis de 4 semitonos/s), a
        // corrección sóltaa en proporción á «transición», para non arrastrala cara á nota de
        // antes. Con transición 0 (robot) non cambia nada: escaleira seca.
        const double inst = (centre - st.prevCentre) * sr / std::max (1.0, dt);
        st.prevCentre = centre;
        st.slope += (clampd (inst, -60.0, 60.0) - st.slope) * alphaFor (30.0, dt);
        const double glide = clampd ((std::abs (st.slope) - 4.0) / 8.0, 0.0, 1.0);
        const double relax = clampd (set.transitionMs / 60.0, 0.0, 1.0) * glide;
        double want = 0.0;
        if (! free)
        {
            const bool outside = std::abs (err) * 100.0 >= set.tolCents;
            st.tolTimer = outside ? st.tolTimer + dt : 0.0;
            if (outside && st.tolTimer >= set.tolMs * 0.001 * sr) want = err * set.amount * (1.0 - relax);
        }
        st.cS += (want - st.cS) * alphaFor (set.speedMs, dt);
        outMidi = centre + st.cS + set.vibrato * vib + transpose + bend;
        return outMidi - m;
    }

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

    // ¿é vibrato de verdade? Mira os cruces por cero da banda de vibrato: se os medios
    // ciclos duran 60–140 ms, parécense entre si e a oscilación pasa de ~12 cents, a
    // confianza sobe; se son irregulares (a inseguridade dunha voz), baixa e esa
    // oscilación corríxese coma calquera desafinación.
    void updateVibratoConfidence (CorrState& st, double band, double dt) const
    {
        st.clock += dt;
        st.vibEnv = std::max (std::abs (band), st.vibEnv * std::exp (-dt / (0.15 * sr)));
        const int sg = band > 0.0 ? 1 : (band < 0.0 ? -1 : 0);
        if (sg != 0 && st.vibSign != 0 && sg != st.vibSign)
        {
            const double half = (st.clock - st.lastZc) / sr;
            const bool rate = half > 0.060 && half < 0.140;
            const bool steady = st.lastHalf > 0 && half / st.lastHalf > 0.7 && half / st.lastHalf < 1.43;
            const bool deep = st.vibEnv > 0.08;
            st.vibConf = (rate && steady && deep) ? std::min (1.0, st.vibConf + 0.5) : std::max (0.0, st.vibConf - 0.5);
            st.lastHalf = rate ? half : 0.0;
            st.lastZc = st.clock;
        }
        if (sg != 0) st.vibSign = sg;
        // sen cruces hai máis de 160 ms: xa non é vibrato
        if ((st.clock - st.lastZc) / sr > 0.16) st.vibConf *= std::exp (-dt / (0.1 * sr));
    }

private:
    const Settings& set;
    double sr = 48000;
};

} // namespace tune
