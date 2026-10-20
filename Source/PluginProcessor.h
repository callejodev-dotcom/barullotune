#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "tune/Engine.h"

namespace scales
{
    // intervalos sobre a tónica (bit i = semitono i)
    struct Scale { const char* name; uint16_t mask; };
    inline const std::array<Scale, 10>& all()
    {
        static const std::array<Scale, 10> s { {
            { "Cromática",         0x0FFF },
            { "Maior",             0b101010110101 },
            { "Menor natural",     0b010110101101 },
            { "Menor harmónica",   0b100110101101 },
            { "Pentatónica maior", 0b001010010101 },
            { "Pentatónica menor", 0b010010101001 },
            { "Blues",             0b010011101001 },
            { "Dórica",            0b011010101101 },
            { "Mixolidia",         0b011010110101 },
            { "Personalizada",     0 },
        } };
        return s;
    }
    constexpr int custom = 9;
    inline juce::String noteName (int pc)
    {
        static const char* n[12] = { "Do", "Do♯", "Re", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si" };
        return juce::String::fromUTF8 (n[((pc % 12) + 12) % 12]);
    }
    // nome con oitava (Do central = Do3, coma nos outros Barullos)
    inline juce::String noteWithOctave (int midi) { return noteName (midi) + juce::String (midi / 12 - 2); }
}

class TuneProcessor : public juce::AudioProcessor
{
public:
    TuneProcessor();
    ~TuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Barullo Tune"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // máscara de notas permitidas (bit = clase de altura absoluta, 0 = Do)
    uint16_t effectiveMask() const;
    // a interface pulsa unha tecla da escala: pasa a «Personalizada» se fai falta
    void toggleScaleNote (int pc);
    void setScalePreset (int index);

    // para a interface: historia do ton (unha trama cada 256 mostras)
    struct Frame { float inMidi, outMidi; int16_t target; uint8_t voiced; };
    static constexpr int fifoSize = 2048;
    juce::AbstractFifo fifo { fifoSize };
    std::array<Frame, fifoSize> frames {};
    std::atomic<float> nowIn { 0 }, nowOut { 0 }, nowHz { 0 }, nowCents { 0 };
    std::atomic<int> nowTarget { -1 }, nowHeld { 0 };
    std::atomic<bool> nowVoiced { false };
    std::atomic<float> inLevel { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void pushFrame();

    tune::Engine engine;
    std::vector<float> dryDelay;
    int dryPos = 0;
    juce::SmoothedValue<float> mixSm, gainSm;
    int frameCount = 0;
    float levelAcc = 0;

    std::atomic<float>* pModo, *pTonica, *pEscala, *pVel, *pCant, *pHum, *pTrans, *pLa, *pMest, *pVol;
    std::array<std::atomic<float>*, 12> pNota {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneProcessor)
};
