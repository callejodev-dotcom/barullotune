// Barullo Tune — a decisión da nota de destino.
//
// A nota permitida máis próxima ao ton, con histérese para que o vibrato e as voces
// que oscilan entre dúas notas non fagan bailar o destino. A análise ten unha (para a
// interface e as marcas de ciclo) e cada voz de saída ten a súa, alimentada co ton do
// punto que está lendo: así a nota cambia onde cambia a voz que se escoita, sen o
// atraso do detector.
#pragma once
#include "Correction.h"

namespace tune {

// teclas MIDI tocadas. O pitch bend lévase por canle (MPE: cada nota na súa canle, e a
// canle 1 é a mestra, que dobra todas); sen MPE todo vai pola canle 1 e é coma sempre.
struct MidiState
{
    uint8_t held[128] {};
    uint8_t noteChannel[128] {};
    uint16_t heldMask = 0;
    int lastNote = -1;
    double bendCh[17] {};   // −1..1, por canle (1..16)

    void noteOn (int note, int channel = 1)
    {
        if (note < 0 || note >= 128) return;
        held[note] = 1; noteChannel[note] = (uint8_t) std::max (1, std::min (16, channel)); lastNote = note; recompute();
    }
    void noteOff (int note)
    {
        if (note < 0 || note >= 128) return;
        held[note] = 0; recompute();
        if (note == lastNote) { lastNote = -1; for (int i = 127; i >= 0; --i) if (held[i]) { lastNote = i; break; } }
    }
    void allNotesOff() { for (auto& h : held) h = 0; heldMask = 0; lastNote = -1; }
    void setPitchBend (int value14, int channel = 1) { bendCh[std::max (1, std::min (16, channel))] = (value14 - 8192) / 8192.0; }
    // o bend da última nota: o da súa canle e, se non é a mestra, tamén o da mestra
    double bend() const
    {
        if (lastNote < 0) return bendCh[1];
        const int ch = noteChannel[lastNote];
        return bendCh[ch] + (ch != 1 ? bendCh[1] : 0.0);
    }

private:
    void recompute()
    {
        heldMask = 0;
        for (int i = 0; i < 128; ++i) if (held[i]) heldMask |= (uint16_t) (1u << (i % 12));
    }
};

// ¿pódese ir a esta nota? (escala, notas libres ou as teclas MIDI)
inline bool noteAllowed (const Settings& set, const MidiState& midi, int note)
{
    if (set.useNoteCents && note >= 0 && note < 128 && set.noteBlocked[note]) return false;
    const int pc = ((note % 12) + 12) % 12;
    if ((set.mode == Settings::MidiScale || set.mode == Settings::MidiNote) && midi.heldMask != 0) return (midi.heldMask >> pc) & 1;
    const int deg = ((pc - set.key) % 12 + 12) % 12;
    return ((set.scale | set.freeMask) >> deg) & 1;
}

// nota libre: a voz pode ir a ela e non se corrixe
inline bool noteFree (const Settings& set, const MidiState& midi, int note)
{
    if ((set.mode == Settings::MidiScale || set.mode == Settings::MidiNote) && midi.heldMask != 0) return false;
    const int deg = (((note % 12) + 12) % 12 - set.key + 12) % 12;
    return (set.freeMask >> deg) & 1;
}

// A nota dunha armonía para a nota principal «lead». En escala: o intervalo en graos da
// escala (a terceira dunha nota da escala é a nota da escala dous pasos arriba, maior ou
// menor segundo onde caia). Se a escala é cromática (ou ten moi poucas notas), os
// intervalos son os da escala maior: terceira = 4 semitonos, quinta = 7... En acorde MIDI:
// a voz i vai á i-ésima tecla tocada (de grave a aguda). Devolve −1 se non hai nota.
inline int harmonyNote (const Settings& set, const MidiState& midi, int lead, int index)
{
    if (lead < 0 || index < 0 || index >= Settings::maxHarmonies) return -1;
    if (set.harmonyMode == Settings::HarmonyMidiChord)
    {
        int k = 0;
        for (int nt = 0; nt < 128; ++nt)
            if (midi.held[nt] && k++ == index) return nt;
        return -1;
    }
    const int deg = std::max (-14, std::min (14, set.harm[index].degrees));
    uint16_t mask = 0;
    if ((set.mode == Settings::MidiScale || set.mode == Settings::MidiNote) && midi.heldMask != 0) mask = midi.heldMask;
    else for (int pc = 0; pc < 12; ++pc) if ((set.scale >> (((pc - set.key) % 12 + 12) % 12)) & 1) mask |= (uint16_t) (1u << pc);
    int count = 0; for (int pc = 0; pc < 12; ++pc) count += (mask >> pc) & 1;
    if (count >= 12 || count < 3)
    {
        static const int major[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        const int a = std::abs (deg), semis = (a / 7) * 12 + major[a % 7];
        return lead + (deg < 0 ? -semis : semis);
    }
    auto in = [mask] (int nt) { return (mask >> (((nt % 12) + 12) % 12)) & 1; };
    int nt = lead;
    while (! in (nt)) --nt;   // unha nota libre ou fóra: cóntase desde a da escala de debaixo
    const int dir = deg < 0 ? -1 : 1;
    for (int k = 0; k < std::abs (deg); ++k) { nt += dir; while (! in (nt)) nt += dir; }
    return nt;
}

class NoteDecider
{
public:
    NoteDecider (const Settings& s, const MidiState& m) : set (s), midi (m), vibCorr (s) {}

    void prepare (double sampleRate) { sr = sampleRate; vibCorr.prepare (sr); }
    void reset() { active = false; target = -1; gliding = false; }

    // m: ton agora (MIDI, con vibrato e todo); dt: mostras desde a chamada anterior.
    // Devolve a nota de destino.
    int update (double m, double dt)
    {
        const bool was = active;
        active = true;
        // ton para decidir a nota: suavizado 40 ms (sen vibrato), para que un vibrato ancho
        // ou unha voz a medio camiño entre dúas notas non faga saltar o destino
        const double prevMd = mSmooth;
        if (! was) { mSmooth = m; candidate = -1; candHeld = 0; decSlope = 0; }
        else mSmooth += (m - mSmooth) * (1.0 - std::exp (-dt / (0.04 * sr)));
        // glisandos: o suavizado atrasa o ton uns 50 ms e, nun glisando rápido, a nota de
        // destino quedaba ata dous semitonos detrás da voz. Se a voz percorre máis dun
        // semitono no mesmo sentido e non é un vibrato regular, o suavizado volve empezar
        // no ton de agora (un filtro que aplane o vibrato sen atrasar os glisandos non existe)
        if (! was) { runStart = runExt = m; runDir = 0; resetVibrato (m); }
        else
        {
            const double band = clampd (vibCorr.vibratoBand (vibState, m, dt), -1.5, 1.5);
            vibCorr.updateVibratoConfidence (vibState, band, dt);
            if (runDir >= 0 && m > runExt) { runExt = m; runDir = 1; }
            else if (runDir <= 0 && m < runExt) { runExt = m; runDir = -1; }
            if (runDir > 0 && m < runExt - 0.15) { runDir = -1; runStart = runExt; runExt = m; }       // volta atrás
            else if (runDir < 0 && m > runExt + 0.15) { runDir = 1; runStart = runExt; runExt = m; }
            gliding = std::abs (runExt - runStart) > 1.0 && vibState.vibConf < 0.5;
            // un salto de máis de 0,7 semitonos dunha vez non é vibrato nin portamento: é
            // outra nota (o vibrato máis ancho e rápido move 0,15 en 4 ms)
            const bool jump = std::abs (m - lastM) > 0.7;
            if (jump) { resetVibrato (m); runStart = runExt = m; runDir = 0; }
            if (gliding || jump) mSmooth = m;
        }
        lastM = m;
        const double md = mSmooth;
        if (was) decSlope += ((md - prevMd) / (dt / sr) - decSlope) * (1.0 - std::exp (-dt / (0.0112 * sr)));   // semitonos/s

        int t = target;
        if (set.forcedNote >= 0) t = set.forcedNote;   // edición: a nota editada á man
        else if (set.mode == Settings::MidiNote && midi.lastNote >= 0)
        {
            // nota exacta: a oitava máis próxima á voz
            t = midi.lastNote;
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
                if (noteAllowed (set, midi, k) && std::abs (md - set.pitchOf (k)) < bd) { bd = std::abs (md - set.pitchOf (k)); best = k; }
            if (best < 0) best = c;
            const bool keep = was && t >= 0 && noteAllowed (set, midi, t) && std::abs (md - set.pitchOf (t)) < 0.6;
            if (! keep && best != t)
            {
                if (best != candidate) { candidate = best; candHeld = 0; }
                candHeld += dt;
                // se a voz vai claramente cara á nota nova (máis de 5 semitonos/s), acéptase xa
                const bool movingThere = std::abs (decSlope) > 5.0 && (best - t) * decSlope > 0 && std::abs (md - set.pitchOf (t)) > 0.5;
                if (! was || t < 0 || ! noteAllowed (set, midi, t) || std::abs (md - set.pitchOf (t)) > 0.9 || movingThere || candHeld >= 0.025 * sr) t = best;
            }
            else candidate = -1;
        }
        target = t;
        return t;
    }

    int target = -1;
    bool active = false, gliding = false;

private:
    // o filtro do vibrato empeza no ton de agora (desde 0 vería un chanzo enorme e
    // tardaría medio segundo en recoñecer o vibrato)
    void resetVibrato (double m) { vibState = {}; vibState.ic2 = m; }

    const Settings& set;
    const MidiState& midi;
    Corrector vibCorr;
    CorrState vibState;
    double sr = 48000;
    double mSmooth = 0, candHeld = 0, decSlope = 0, lastM = 0;
    double runStart = 0, runExt = 0;
    int runDir = 0, candidate = -1;
};

} // namespace tune
