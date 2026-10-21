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

    Engine() : an (*this), direct (*this, an), studio (*this, an) {}
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setPitchBend (int value14) { an.setPitchBend (value14); }

    void prepare (double sampleRate)
    {
        an.prepare (sampleRate);
        direct.prepare (sampleRate);
        studio.prepare (sampleRate);
        configure (quality, range);
    }

    // cambia o xeito e o rango (cambia a latencia; volve empezar)
    void configure (int q, int r)
    {
        quality = std::max (0, std::min (2, q));
        range = std::max (0, std::min ((int) numRanges - 1, r));
        an.setRange (range);
        direct.configure (quality == Precise);
        latencySamples = quality == Studio ? StudioVoice::latencyFor (an.pMax) : direct.latency();
        an.latencySamples = latencySamples;
        reset();
    }

    int latency() const { return latencySamples; }
    int currentQuality() const { return quality; }
    int currentRange() const { return range; }

    void reset()
    {
        an.reset();
        studio.reset (latencySamples);
        direct.reset();
    }

    void noteOn (int note) { an.noteOn (note); }
    void noteOff (int note) { an.noteOff (note); }
    void allNotesOff() { an.allNotesOff(); }

    float process (float x) { float y = 0; processFrame (&x, &y, 1); return y; }

    // unha mostra de 1 ou 2 canles: o ton detéctase no centro (L+R)/2 e as dúas canles
    // corríxense exactamente igual, así a imaxe estéreo non se move
    void processFrame (const float* x, float* y, int numChannels)
    {
        an.push (x, numChannels);
        if (quality != Studio) an.slideUpdate();
        if (quality == Studio) studio.process (y); else direct.process (y);
        an.advance();
    }

    // para a interface
    bool isVoiced() const { return an.voiced; }
    double detectedHz() const { return an.detectedHz; }
    double correctionCents() const { return an.correctionCents; }
    double inputMidi() const { return an.inputMidi; }
    double outputMidi() const { return an.outputMidi; }
    double clarity() const { return an.detector.clarity; }
    uint16_t heldPitchClasses() const { return an.heldMask; }
    int targetNote() const { return an.target; }

private:
    Analysis an;
    DirectVoice direct;
    StudioVoice studio;
    int quality = Direct, range = Analysis::General;
    int latencySamples = 64;
};

} // namespace tune
