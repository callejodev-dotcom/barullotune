// Barullo Tune — motor de corrección de afinación.
//
// Unha análise común (Analysis: detector de ton tipo YIN, marcas de ciclo, ton
// deslizante e decisión da nota) e tres xeitos de mover o ton:
//
//  * Directo (latencia < 1 ms) e Preciso (12 ms): DirectVoice. A voz lese a outra
//    velocidade e salta un ciclo enteiro cando se adianta ou se atrasa; a corrección de
//    formantes sen latencia devolve o timbre ao transpor.
//  * Estudio (latencia 15–42 ms segundo o rango): StudioVoice, TD-PSOLA. Mantén os
//    formantes e permite movelos á parte.
//
// O modelo de corrección (Correction.h) é o mesmo para todos.
// Sen reservas de memoria no fío de audio: todo se reserva en prepare().
#pragma once
#include "DirectVoice.h"
#include "StudioVoice.h"

namespace tune {

class Engine : public Settings
{
public:
    enum Quality { Direct = 0, Precise = 1, Studio = 2 };
    using Range = Analysis::Range;
    static constexpr int numRanges = Analysis::numRanges;
    static void rangeHz (int r, double& lo, double& hi) { Analysis::rangeHz (r, lo, hi); }

    Engine()
        : an (*this), direct (*this, an), studio (*this, an),
          harmDirect { DirectVoice (*this, an, { true, 0 }), DirectVoice (*this, an, { true, 1 }), DirectVoice (*this, an, { true, 2 }), DirectVoice (*this, an, { true, 3 }) },
          harmStudio { StudioVoice (*this, an, { true, 0 }), StudioVoice (*this, an, { true, 1 }), StudioVoice (*this, an, { true, 2 }), StudioVoice (*this, an, { true, 3 }) }
    {}
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setPitchBend (int value14, int channel = 1) { an.midi.setPitchBend (value14, channel); }

    void prepare (double sampleRate)
    {
        an.prepare (sampleRate);
        direct.prepare (sampleRate);
        studio.prepare (sampleRate);
        for (auto& h : harmDirect) h.prepare (sampleRate);
        for (auto& h : harmStudio) h.prepare (sampleRate);
        for (auto& h : harmDirect) h.configure (false);
        configure (quality, range);
    }

    // cambia o xeito e o rango (cambia a latencia) e volve empezar de cero
    void configure (int q, int r) { setMode (q, r, true); }

    // o mesmo, en marcha: a análise segue (a historia da voz, as marcas de ciclo) e só
    // as voces de saída empezan de novo no punto de agora; así non hai que agardar a que
    // volva entrar a voz (antes, en Estudio, o son volvía de golpe aos 36 ms)
    void switchTo (int q, int r) { setMode (q, r, false); }

    // o atraso real da saída (para aliñar a orixinal na mestura e no bypass)
    int latency() const { return latencySamples; }
    // o que se lle declara ao DAW. En Directo, 0: o que queda (12 mostras, 0,25 ms) é o
    // mínimo do interpolador e, corrixindo, a lectura vai ademais ata un ciclo por detrás,
    // así que non é un atraso fixo que o DAW poida compensar
    int reportedLatency() const { return quality == Direct ? 0 : latencySamples; }
    int currentQuality() const { return quality; }
    int currentRange() const { return range; }

    void reset()
    {
        an.reset();
        studio.reset (latencySamples);
        direct.reset();
        for (auto& h : harmDirect) h.reset();
        for (auto& h : harmStudio) h.reset (latencySamples);
        for (auto& w : harmWasOn) w = false;
    }

    void noteOn (int note, int channel = 1) { an.midi.noteOn (note, channel); }
    void noteOff (int note) { an.midi.noteOff (note); }
    void allNotesOff() { an.midi.allNotesOff(); }

    float process (float x) { float y = 0; processFrame (&x, &y, 1); return y; }

    // unha mostra de 1 ou 2 canles: o ton detéctase no centro (L+R)/2 e as dúas canles
    // corríxense exactamente igual, así a imaxe estéreo non se move. harmOut (se non é
    // nulo): a saída mono de cada armonía (maxHarmonies), sen nivel nin panorama
    void processFrame (const float* x, float* y, int numChannels, float* harmOut = nullptr)
    {
        an.push (x, numChannels);
        if (quality != Studio) an.slideUpdate();
        if (quality == Studio) studio.process (y); else direct.process (y);
        if (harmOut != nullptr)
            for (int h = 0; h < maxHarmonies; ++h)
            {
                harmOut[h] = 0.0f;
                if (! harm[h].on) { harmWasOn[h] = false; continue; }
                if (! harmWasOn[h]) { harmDirect[h].restart (an.n); harmStudio[h].restart (an.n, latencySamples); harmWasOn[h] = true; }
                if (quality == Studio) harmStudio[h].process (&harmOut[h]); else harmDirect[h].process (&harmOut[h]);
            }
        an.advance();
    }

    // para a interface
    bool isVoiced() const { return an.voiced; }
    double detectedHz() const { return an.detectedHz; }
    double correctionCents() const { return an.correctionCents; }
    double inputMidi() const { return an.inputMidi; }
    double outputMidi() const { return an.outputMidi; }
    double clarity() const { return an.detector.clarity; }
    const PitchDetector& detector() const { return an.detector; }
    uint16_t heldPitchClasses() const { return an.midi.heldMask; }
    int targetNote() const { return an.target; }

private:
    void setMode (int q, int r, bool full)
    {
        const int oldRange = range;
        quality = std::max (0, std::min (2, q));
        range = std::max (0, std::min ((int) numRanges - 1, r));
        if (full || range != oldRange) an.setRange (range);
        direct.configure (quality == Precise);
        for (auto& h : harmDirect) h.configure (quality == Precise);
        latencySamples = quality == Studio ? StudioVoice::latencyFor (an.pMax) : direct.latency();
        an.latencySamples = latencySamples;
        if (full) reset();
        else
        {
            direct.restart (an.n); studio.restart (an.n, latencySamples);
            for (auto& h : harmDirect) h.restart (an.n);
            for (auto& h : harmStudio) h.restart (an.n, latencySamples);
        }
    }

    Analysis an;
    DirectVoice direct;
    StudioVoice studio;
    DirectVoice harmDirect[maxHarmonies];
    StudioVoice harmStudio[maxHarmonies];
    bool harmWasOn[maxHarmonies] {};
    int quality = Direct, range = Analysis::General;
    int latencySamples = 64;
};

} // namespace tune
