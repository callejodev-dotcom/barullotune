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
    double flex = 0.0;         // 0..1: canto máis lonxe da nota, menos se corrixe (blue notes, arrastres)
    double humanize = 0.0;     // 0..1: nas notas longas a corrección vaise relaxando
    double tuneCents[12] {};   // afinación: cents de cada clase de altura (absoluta, 0 = Do)
    // afinación nota a nota (MTS-ESP: cada nota pode ir a onde diga o mestre, sen repetirse
    // por oitavas); se useNoteCents, manda isto e non tuneCents
    bool useNoteCents = false;
    double noteCents[128] {};
    uint8_t noteBlocked[128] {};   // notas que o mestre de MTS-ESP non usa

    // edición: unha nota editada á man no punto que se escoita (−1: ningunha). Mentres dura,
    // o destino é esa nota (máis forcedCents) e corríxese enteira: sen tolerancia,
    // flexibilidade nin humanizar (a velocidade, a transición e o vibrato, coma sempre)
    int forcedNote = -1;
    double forcedCents = 0.0;
    bool isForced (int tgt) const { return forcedNote >= 0 && tgt == forcedNote; }

    // a altura (MIDI, con decimais) á que vai unha nota coa afinación
    double pitchOf (int note) const
    {
        if (useNoteCents && note >= 0 && note < 128) return note + noteCents[note] * 0.01;
        return note + tuneCents[((note % 12) + 12) % 12] * 0.01;
    }

    // vibrato xerado (enriba do da voz, que se escala con «vibrato»)
    double vibDepth = 0.0;     // cents (0 = sen vibrato xerado)
    double vibRate = 5.5;      // Hz
    double vibDelayMs = 300.0; // desde o comezo da nota
    double vibVariation = 0.3; // 0..1: a velocidade e o fondo varían amodo, coma nunha voz
    double tremolo = 0.0;      // 0..1: modulación de nivel ao compás do vibrato

    // armonías: voces novas a partir da voz cantada
    static constexpr int maxHarmonies = 4;
    enum HarmonyMode { HarmonyScale = 0, HarmonyMidiChord = 1 };
    struct Harmony
    {
        bool on = false;
        int degrees = 2;        // intervalo en graos da escala: +2 = terceira arriba, −7 = oitava abaixo
        double detune = 0.0;    // cents (para que non soe pegada á principal)
        double formant = 0.0;   // semitonos (outra voz: máis grande ou máis pequena)
    };
    Harmony harm[maxHarmonies];
    int harmonyMode = HarmonyScale;
};

// o que fai unha voz de saída: a principal (todas as canles; o que se amosa) ou unha
// armonía (o centro mono, cun intervalo sobre a nota da principal)
struct VoiceRole
{
    bool harmony = false;
    int index = 0;
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
    double noteAge = 0;   // mostras desde que empezou a nota (para humanizar e o vibrato xerado)
    // vibrato xerado: fase, variación lenta (ruído filtrado) e a modulación de nivel que sae
    double genPhase = 0, genRateVar = 0, genDepthVar = 0, genRateGoal = 0, genDepthGoal = 0, genClock = 0;
    uint32_t rng = 0x9E3779B9u;
    bool seeded = false;
    double ampMod = 1.0;
};

class Corrector
{
public:
    explicit Corrector (const Settings& s) : set (s) {}
    void prepare (double sampleRate) { sr = sampleRate; }
    // cada voz (a principal, cada armonía) co seu vibrato xerado: outra fase e outra variación
    void setVoice (int index) { phase0 = index * 2.1; seed = 0x9E3779B9u ^ ((uint32_t) index * 0x85EBCA6Bu); if (seed == 0) seed = 1; }

    double alphaFor (double ms, double dt) const { return ms <= 0.0 ? 1.0 : 1.0 - std::exp (-dt / (ms * 0.001 * sr)); }

    // corrección en semitonos para unha voz en m (MIDI) que vai a tgt; dt = mostras desde o
    // paso anterior. free: a nota é libre (non se corrixe); bend: pitch bend en semitonos
    double step (CorrState& st, double m, int tgt, double dt, bool free, double bend, double& outMidi) const
    {
        const double transpose = set.transpose;
        const bool forced = set.isForced (tgt);
        if (forced) { free = false; bend += set.forcedCents * 0.01; }
        if (! st.active)
        {
            // comezo dunha nota: xa afinada
            st.active = true; st.tS = set.pitchOf (tgt); st.vibRamp = 0; st.tolTimer = 0;
            st.ic1 = 0; st.ic2 = m;
            st.prevCentre = m; st.slope = 0;
            st.clock = 0; st.lastZc = -1e9; st.lastHalf = 0; st.vibEnv = 0; st.vibConf = 0; st.vibSign = 0;
            st.noteAge = 0;
            const double err = st.tS - m;
            st.cS = forced ? err : (free || std::abs (err) * 100.0 < set.tolCents) ? 0.0 : err * set.amount * flexFactor (err);
            st.lastTarget = tgt;
            st.genPhase = phase0; st.ampMod = 1.0;
            if (! st.seeded) { st.rng = seed; st.seeded = true; }
            outMidi = m + st.cS + transpose + bend;
            return st.cS + transpose + bend;
        }
        if (tgt != st.lastTarget) { st.vibRamp = 0; st.noteAge = 0; }   // nota nova: o vibrato volve entrar amodo
        st.noteAge += dt;
        st.lastTarget = tgt;
        st.tS += (set.pitchOf (tgt) - st.tS) * alphaFor (set.transitionMs, dt);
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
        if (forced) want = err * (1.0 - relax);
        else if (! free)
        {
            const bool outside = std::abs (err) * 100.0 >= set.tolCents;
            st.tolTimer = outside ? st.tolTimer + dt : 0.0;
            if (outside && st.tolTimer >= set.tolMs * 0.001 * sr) want = err * set.amount * (1.0 - relax) * flexFactor (err);
        }
        st.cS += (want - st.cS) * alphaFor (forced ? set.speedMs : speedFor (st), dt);
        outMidi = centre + st.cS + set.vibrato * vib + transpose + bend + generatedVibrato (st, dt);
        return outMidi - m;
    }

    // vibrato xerado (semitonos): un seno que entra despois do retardo (rampa de 200 ms),
    // coa velocidade e o fondo variando amodo (±12 % e ±25 % co 100 % de variación), e
    // deixa en st.ampMod a modulación de nivel (trémolo) ao mesmo compás
    double generatedVibrato (CorrState& st, double dt) const
    {
        st.ampMod = 1.0;
        if (set.vibDepth <= 0.0 && set.tremolo <= 0.0) return 0.0;
        const double t = dt / sr;
        // variación: cada ~250 ms un obxectivo novo ao chou, e achégase a el suavemente
        st.genClock += t;
        if (st.genClock >= 0.25)
        {
            st.genClock -= 0.25;
            auto rnd = [&st] { st.rng ^= st.rng << 13; st.rng ^= st.rng >> 17; st.rng ^= st.rng << 5; return (double) st.rng / 4294967295.0 * 2.0 - 1.0; };
            st.genRateGoal = rnd(); st.genDepthGoal = rnd();
        }
        const double a = 1.0 - std::exp (-t / 0.2);
        st.genRateVar += (st.genRateGoal - st.genRateVar) * a;
        st.genDepthVar += (st.genDepthGoal - st.genDepthVar) * a;
        const double var = clampd (set.vibVariation, 0.0, 1.0);
        const double rate = clampd (set.vibRate, 0.5, 15.0) * (1.0 + 0.12 * var * st.genRateVar);
        st.genPhase += 2.0 * kPi * rate * t;
        if (st.genPhase > 2.0 * kPi) st.genPhase -= 2.0 * kPi;
        const double ramp = clampd ((st.noteAge / sr - set.vibDelayMs * 0.001) / 0.2, 0.0, 1.0);
        const double sn = std::sin (st.genPhase) * ramp;
        st.ampMod = 1.0 + 0.5 * clampd (set.tremolo, 0.0, 1.0) * sn;
        return set.vibDepth * 0.01 * (1.0 + 0.25 * var * st.genDepthVar) * sn;
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

    // flexibilidade: ata «zona» cents da nota corríxese todo; de alí a 25 cents máis, cada
    // vez menos; máis lonxe, nada (a voz vai a outro sitio a propósito). Flexibilidade 0:
    // sempre todo; 100 %: só os últimos 5 cents
    double flexFactor (double err) const
    {
        if (set.flex <= 0.0) return 1.0;
        const double d = std::abs (err) * 100.0, zone = 60.0 * (1.0 - clampd (set.flex, 0.0, 1.0)) + 5.0;
        const double u = clampd ((d - zone) / 25.0, 0.0, 1.0);
        return 1.0 - u * u * (3.0 - 2.0 * u);
    }

    // humanizar: as notas curtas co tempo de «velocidade»; nas longas, despois de 150 ms,
    // a velocidade vaise relaxando (ata +250 ms aos 500 ms), así a nota sostida respira
    double speedFor (const CorrState& st) const
    {
        if (set.humanize <= 0.0) return set.speedMs;
        const double ramp = clampd ((st.noteAge / sr - 0.15) / 0.35, 0.0, 1.0);
        return set.speedMs + clampd (set.humanize, 0.0, 1.0) * 250.0 * ramp;
    }

private:
    const Settings& set;
    double sr = 48000;
    double phase0 = 0.0;
    uint32_t seed = 0x9E3779B9u;
};

} // namespace tune
