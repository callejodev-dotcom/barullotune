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
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "libre" + String (i), 1 }, U ("Libre ") + scales::noteName (i), false));

    auto msFmt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("0 ms") : String (roundToInt (v)) + " ms"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velocidade", 1 }, U ("Velocidade"),
                                                  NormalisableRange<float> (0.0f, 800.0f, 0.0f, 0.3f), 20.0f, msFmt));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "transicion", 1 }, U ("Transición"),
                                                  NormalisableRange<float> (0.0f, 800.0f, 0.0f, 0.3f), 40.0f, msFmt));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "ligar", 1 }, U ("Ligar velocidade e transición"), false,
                                                 AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tolerancia", 1 }, U ("Tolerancia"), NormalisableRange<float> (0.0f, 40.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("non") : String (roundToInt (v)) + " c"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "toltempo", 1 }, U ("Tolerancia (tempo)"), NormalisableRange<float> (0.0f, 300.0f, 0.0f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("non") : String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibrato", 1 }, U ("Vibrato"), NormalisableRange<float> (0.0f, 200.0f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cantidade", 1 }, U ("Cantidade"), NormalisableRange<float> (0.0f, 100.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "transpor", 1 }, U ("Transpor"), -12, 12, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "la", 1 }, U ("La"), NormalisableRange<float> (415.3f, 466.2f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mestura", 1 }, U ("Mestura"), NormalisableRange<float> (0.0f, 100.0f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, U ("Volume"), NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    // cambian a latencia: non se automatizan
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "calidade", 1 }, U ("Xeito"), StringArray { U ("Directo"), U ("Estudio") }, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rango", 1 }, U ("Voz"),
        StringArray { U ("Baixo"), U ("Barítono"), U ("Tenor"), U ("Alto"), U ("Mezzo"), U ("Soprano"), U ("Calquera") }, 6,
        AudioParameterChoiceAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "comparar", 1 }, U ("Comparar (voz orixinal)"), false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "formantes", 1 }, U ("Formantes"), NormalisableRange<float> (-7.0f, 7.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (v, 1); })));
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
    pTransicion = params.getRawParameterValue ("transicion");
    pTol = params.getRawParameterValue ("tolerancia");
    pTolT = params.getRawParameterValue ("toltempo");
    pVib = params.getRawParameterValue ("vibrato");
    pTrans = params.getRawParameterValue ("transpor");
    pLa = params.getRawParameterValue ("la");
    pMest = params.getRawParameterValue ("mestura");
    pVol = params.getRawParameterValue ("volume");
    pCal = params.getRawParameterValue ("calidade");
    pRango = params.getRawParameterValue ("rango");
    pForm = params.getRawParameterValue ("formantes");
    pComp = params.getRawParameterValue ("comparar");
    for (int i = 0; i < 12; ++i) pNota[(size_t) i] = params.getRawParameterValue ("nota" + juce::String (i));
    for (int i = 0; i < 12; ++i) pLibre[(size_t) i] = params.getRawParameterValue ("libre" + juce::String (i));
    for (auto& h : keyHist) h = 0;

    engine.prepare (44100.0);
    applyEngineSettings();
    setLatencySamples (latencyNow.load());
}

// xeito e rango cambian a latencia: o motor volve empezar e avísase ao DAW
void TuneProcessor::applyEngineSettings()
{
    const int q = juce::jlimit (0, 1, (int) pCal->load()), r = juce::jlimit (0, (int) tune::Engine::numRanges - 1, (int) pRango->load());
    if (q != engine.currentQuality() || r != engine.currentRange() || latencyNow.load() != engine.latency())
    {
        engine.configure (q, r);
        latencyNow = engine.latency();
        triggerAsyncUpdate();
    }
}

void TuneProcessor::handleAsyncUpdate() { setLatencySamples (latencyNow.load()); }

void TuneProcessor::startKeyListening()
{
    for (auto& h : keyHist) h = 0;
    keyHeard = 0;
    keyListening = true;
}

bool TuneProcessor::finishKeyIfReady()
{
    if (! keyListening.load() || keyHeard.load() < keyListenSeconds) return false;
    float h[12];
    for (int i = 0; i < 12; ++i) h[i] = keyHist[(size_t) i].load();
    const auto k = scales::detectKey (h);
    setParamPlain ("tonica", (float) k.first);
    setParamPlain ("escala", (float) k.second);
    keyListening = false;
    return true;
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
    latencyNow = -1;
    applyEngineSettings();
    cancelPendingUpdate();
    setLatencySamples (latencyNow.load());
    // a voz seca vai atrasada o mesmo cá corrixida (o máximo posible: estudio, voz grave)
    const int maxLat = (int) std::ceil (2.5 * (std::ceil (sr / 60.0) + 2)) + 64;
    for (auto& d : dry) d.assign ((size_t) maxLat + 1, 0.0f);
    dryPos = 0;
    mixSm.reset (sr, 0.03); mixSm.setCurrentAndTargetValue (pComp->load() > 0.5f ? 0.0f : pMest->load() * 0.01f);
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
    return scales::rotate (scales::all()[(size_t) sc].mask, (int) pTonica->load());
}

void TuneProcessor::setParamPlain (const juce::String& id, float plain)
{
    if (auto* r = params.getParameter (id))
    {
        r->beginChangeGesture();
        r->setValueNotifyingHost (r->convertTo0to1 (plain));
        r->endChangeGesture();
    }
}

const std::vector<TuneProcessor::Preset>& TuneProcessor::presets()
{
    //                     velocidade transición vibrato tol tolT cantidade formantes transpor xeito (-1 = non cambia)
    static const std::vector<Preset> p {
        { "Natural",        "Corrección clara que respecta o vibrato. O punto de partida.",                 20,  40, 100,  0,   0, 100,  0,   0, -1 },
        { "Suave",          "Só arranxa o que se nota: deixa as pequenas desafinacións e os adornos.",     60, 120, 100, 12,  60,  85,  0,   0, -1 },
        { "Pop",            "Afinado e limpo, co vibrato algo contido.",                                    8,  25,  70,  0,   0, 100,  0,   0, -1 },
        { "Balada",         "Notas longas centradas e vibrato aberto.",                                    40,  90, 120,  8,  40, 100,  0,   0, -1 },
        { "Robot",          "O efecto clásico: salta de nota en seco e sen vibrato.",                       0,   0,   0,  0,   0, 100,  0,   0, -1 },
        { "Trap",           "Duro, pero cun pouco de vida nas notas longas.",                               0,  12,  25,  0,   0, 100,  0,   0, -1 },
        { "Directo seguro", "Para cantar en directo sen latencia: corrección discreta.",                   25,  50, 100,  6,  30,  90,  0,   0,  0 },
        { "Xigante",        "Unha oitava abaixo e voz grande (Estudio).",                                  20,  40, 100,  0,   0, 100, -4, -12,  1 },
        { "Trasno",         "Unha oitava arriba e voz pequena (Estudio).",                                 20,  40, 100,  0,   0, 100,  4,  12,  1 },
    };
    return p;
}

const juce::String TuneProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size()) ? U (presets()[(size_t) index].name) : juce::String();
}

void TuneProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentPreset = index;
    const auto& pr = presets()[(size_t) index];
    setParamPlain ("velocidade", pr.vel);
    setParamPlain ("transicion", pr.transi);
    setParamPlain ("vibrato", pr.vib);
    setParamPlain ("tolerancia", pr.tol);
    setParamPlain ("toltempo", pr.tolT);
    setParamPlain ("cantidade", pr.cant);
    setParamPlain ("formantes", pr.form);
    setParamPlain ("transpor", pr.trans);
    if (pr.quality >= 0) setParamPlain ("calidade", (float) pr.quality);
}

uint16_t TuneProcessor::freeMask() const
{
    uint16_t m = 0;
    for (int i = 0; i < 12; ++i) if (pLibre[(size_t) i]->load() > 0.5f) m |= (uint16_t) (1u << i);
    return m;
}

void TuneProcessor::toggleFreeNote (int pc)
{
    pc = ((pc % 12) + 12) % 12;
    const bool on = (freeMask() >> pc) & 1;
    setParamPlain ("libre" + juce::String (pc), on ? 0.0f : 1.0f);
}

void TuneProcessor::toggleScaleNote (int pc)
{
    const uint16_t m = effectiveMask();
    if ((int) pEscala->load() != scales::custom)
    {
        for (int i = 0; i < 12; ++i) setParamPlain ("nota" + juce::String (i), ((m >> i) & 1) ? 1.0f : 0.0f);
        setParamPlain ("escala", (float) scales::custom);
    }
    const bool on = (m >> pc) & 1;
    setParamPlain ("nota" + juce::String (pc), on ? 0.0f : 1.0f);
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

    if (keyListening.load() && engine.voiced && engine.clarity() > 0.75)
    {
        const float dt = 256.0f / (float) getSampleRate();
        const int pc = ((int) std::lround (engine.inputMidi) % 12 + 12) % 12;
        keyHist[(size_t) pc] = keyHist[(size_t) pc].load() + dt;
        keyHeard = keyHeard.load() + dt;
    }
}

void TuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels(), numIn = juce::jmin (numCh, getTotalNumInputChannels());
    const int n = buffer.getNumSamples();

    applyEngineSettings();
    engine.mode = juce::jlimit (0, 2, (int) pModo->load());
    engine.formant = pForm->load();
    engine.key = 0;
    engine.scale = effectiveMask();
    engine.speedMs = pVel->load();
    engine.amount = pCant->load() * 0.01;
    engine.transitionMs = pTransicion->load();
    engine.tolCents = pTol->load();
    engine.tolMs = pTolT->load();
    engine.vibrato = pVib->load() * 0.01;
    engine.freeMask = freeMask();
    engine.transpose = std::round (pTrans->load());
    engine.refA = pLa->load();
    mixSm.setTargetValue (pComp->load() > 0.5f ? 0.0f : pMest->load() * 0.01f);   // comparar: a voz sen tocar
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (pVol->load()));

    auto it = midi.cbegin();
    const auto end = midi.cend();
    const int dsz = (int) dry[0].size(), lat = juce::jmin (dsz - 1, engine.latency());

    for (int i = 0; i < n; ++i)
    {
        for (; it != end && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
            else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        }
        // estéreo de verdade: o ton detéctase no centro e as dúas canles corríxense igual
        const int ech = numIn >= 2 && numCh >= 2 ? 2 : 1;
        float x[2] = { 0, 0 }, wet[2] = { 0, 0 };
        if (ech == 2) { x[0] = buffer.getReadPointer (0)[i]; x[1] = buffer.getReadPointer (1)[i]; }
        else if (numIn > 0) x[0] = buffer.getReadPointer (0)[i];
        for (auto& v : x) if (! std::isfinite (v)) v = 0;

        engine.processFrame (x, wet, ech);
        int rd = dryPos - lat; if (rd < 0) rd += dsz;
        const float mix = mixSm.getNextValue(), g = gainSm.getNextValue();
        float peakIn = 0, peakOut = 0;
        for (int c = 0; c < ech; ++c)
        {
            dry[(size_t) c][(size_t) dryPos] = x[c];
            const float dx = dry[(size_t) c][(size_t) rd];
            const float y = (dx + (wet[c] - dx) * mix) * g;
            buffer.getWritePointer (c)[i] = y;
            peakIn = juce::jmax (peakIn, std::abs (x[c])); peakOut = juce::jmax (peakOut, std::abs (y));
        }
        if (ech == 1) for (int c = 1; c < numCh; ++c) buffer.getWritePointer (c)[i] = buffer.getReadPointer (0)[i];
        if (++dryPos >= dsz) dryPos = 0;

        levelAcc = juce::jmax (levelAcc * 0.9995f, peakIn);
        outAcc = juce::jmax (outAcc * 0.9995f, peakOut);
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
    outLevel = outAcc;
}

void TuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    params.state.setProperty ("preset", currentPreset, nullptr);
    params.state.setProperty ("ui", uiScale.load(), nullptr);
    auto st = params.copyState();
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void TuneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
        {
            params.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = juce::jlimit (0, (int) presets().size() - 1, (int) params.state.getProperty ("preset", 0));
            uiScale = juce::jlimit (0.75f, 1.5f, (float) (double) params.state.getProperty ("ui", 1.0));
        }
}

juce::AudioProcessorEditor* TuneProcessor::createEditor() { return new TuneEditor (*this); }

#ifndef BARULLO_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TuneProcessor(); }
#endif
