#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
}

juce::AudioProcessorValueTreeState::ParameterLayout TuneProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray modes { U ("Escala"), U ("MIDI: escala"), U ("MIDI: nota") };
    StringArray keys, sc;
    for (int i = 0; i < 12; ++i) keys.add (scales::noteName (i));
    for (auto& s : scales::all()) sc.add (U (s.name));

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "modo", 1 }, U ("Modo"), modes, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "tonica", 1 }, U ("Tónica"), keys, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "escala", 1 }, U ("Escala"), sc, 1));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "nota" + String (i), 1 }, U ("Nota ") + scales::noteName (i), true));

    auto msFmt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("robot") : String (roundToInt (v)) + " ms"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velocidade", 1 }, U ("Velocidade"),
                                                  NormalisableRange<float> (0.0f, 400.0f, 0.0f, 0.4f), 25.0f, msFmt));
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cantidade", 1 }, U ("Cantidade"), NormalisableRange<float> (0.0f, 100.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanizar", 1 }, U ("Humanizar"), NormalisableRange<float> (0.0f, 100.0f), 0.0f, pct));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "transpor", 1 }, U ("Transpor"), -12, 12, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "la", 1 }, U ("La"), NormalisableRange<float> (430.0f, 450.0f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mestura", 1 }, U ("Mestura"), NormalisableRange<float> (0.0f, 100.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, U ("Volume"), NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

TuneProcessor::TuneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Entrada", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Saída", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "BarulloTune", layout())
{
    pModo = params.getRawParameterValue ("modo");
    pTonica = params.getRawParameterValue ("tonica");
    pEscala = params.getRawParameterValue ("escala");
    pVel = params.getRawParameterValue ("velocidade");
    pCant = params.getRawParameterValue ("cantidade");
    pHum = params.getRawParameterValue ("humanizar");
    pTrans = params.getRawParameterValue ("transpor");
    pLa = params.getRawParameterValue ("la");
    pMest = params.getRawParameterValue ("mestura");
    pVol = params.getRawParameterValue ("volume");
    for (int i = 0; i < 12; ++i) pNota[(size_t) i] = params.getRawParameterValue ("nota" + juce::String (i));

    engine.prepare (44100.0);
    setLatencySamples (engine.latency());
}

bool TuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void TuneProcessor::prepareToPlay (double sr, int)
{
    engine.prepare (sr);
    setLatencySamples (engine.latency());
    dryDelay.assign ((size_t) juce::jmax (1, engine.latency()), 0.0f);   // atraso = latencia exacta
    dryPos = 0;
    mixSm.reset (sr, 0.03); mixSm.setCurrentAndTargetValue (pMest->load() * 0.01f);
    gainSm.reset (sr, 0.03); gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVol->load()));
    frameCount = 0; levelAcc = 0;
    fifo.reset();
}

uint16_t TuneProcessor::effectiveMask() const
{
    const int sc = juce::jlimit (0, scales::custom, (int) pEscala->load());
    if (sc == scales::custom)
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i) if (pNota[(size_t) i]->load() > 0.5f) m |= (uint16_t) (1u << i);
        return m;
    }
    const int key = juce::jlimit (0, 11, (int) pTonica->load());
    const uint16_t rel = scales::all()[(size_t) sc].mask;
    return (uint16_t) (((rel << key) | (rel >> (12 - key))) & 0x0FFF);
}

static void setParam (juce::AudioProcessorValueTreeState& p, const juce::String& id, float plain)
{
    if (auto* r = p.getParameter (id))
    {
        r->beginChangeGesture();
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
        r->endChangeGesture();
    }
}

void TuneProcessor::toggleScaleNote (int pc)
{
    const uint16_t m = effectiveMask();
    if ((int) pEscala->load() != scales::custom)
    {
        for (int i = 0; i < 12; ++i) setParam (params, "nota" + juce::String (i), ((m >> i) & 1) ? 1.0f : 0.0f);
        setParam (params, "escala", (float) scales::custom);
    }
    const bool on = (m >> pc) & 1;
    setParam (params, "nota" + juce::String (pc), on ? 0.0f : 1.0f);
}

void TuneProcessor::setScalePreset (int index)
{
    setParam (params, "escala", (float) juce::jlimit (0, scales::custom, index));
}

void TuneProcessor::pushFrame()
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    auto& f = frames[(size_t) (n1 > 0 ? s1 : s2)];
    f.inMidi = (float) engine.inputMidi;
    f.outMidi = (float) engine.outputMidi;
    f.target = (int16_t) engine.targetNote();
    f.voiced = engine.voiced ? 1 : 0;
    fifo.finishedWrite (1);
}

void TuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels(), numIn = juce::jmin (numCh, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();

    engine.mode = juce::jlimit (0, 2, (int) pModo->load());
    engine.key = 0;
    engine.scale = effectiveMask();
    engine.speedMs = pVel->load();
    engine.amount = pCant->load() * 0.01;
    engine.humanize = pHum->load() * 0.01;
    engine.transpose = std::round (pTrans->load());
    engine.refA = pLa->load();
    mixSm.setTargetValue (pMest->load() * 0.01f);
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (pVol->load()));

    auto it = midi.cbegin();
    const auto end = midi.cend();
    const int dsz = (int) dryDelay.size();

    for (int i = 0; i < n; ++i)
    {
        for (; it != end && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
            else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        }
        float x = 0;
        for (int c = 0; c < numIn; ++c) x += buffer.getReadPointer (c)[i];
        if (numIn > 1) x /= (float) numIn;
        if (! std::isfinite (x)) x = 0;

        const float wet = engine.process (x);
        const float dry = dryDelay[(size_t) dryPos];
        dryDelay[(size_t) dryPos] = x;
        if (++dryPos >= dsz) dryPos = 0;

        const float mix = mixSm.getNextValue(), g = gainSm.getNextValue();
        const float y = (dry + (wet - dry) * mix) * g;
        for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] = y;

        levelAcc = juce::jmax (levelAcc * 0.9995f, std::abs (x));
        if (++frameCount >= 256) { frameCount = 0; pushFrame(); }
    }
    for (; it != end; ++it)
    {
        const auto m = (*it).getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
    }

    nowIn = (float) engine.inputMidi; nowOut = (float) engine.outputMidi;
    nowHz = (float) engine.detectedHz; nowCents = (float) engine.correctionCents;
    nowTarget = engine.targetNote(); nowVoiced = engine.voiced;
    nowHeld = engine.heldPitchClasses();
    inLevel = levelAcc;
}

void TuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = params.copyState();
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void TuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* TuneProcessor::createEditor() { return new TuneEditor (*this); }

#ifndef BARULLO_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TuneProcessor(); }
#endif
