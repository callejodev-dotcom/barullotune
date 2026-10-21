#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "libMTSClient.h"

namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

    // Algúns envoltorios (CLAP) pasan o texto en UTF-8 coma se fose ASCII: cada byte un
    // carácter («8ª» chega como «8Âª»). Se é así, recomponse.
    juce::String repairUtf8 (const juce::String& t)
    {
        std::string bytes; bool high = false;
        for (auto c : t) { if (c > 255) return t; if (c >= 128) high = true; bytes.push_back ((char) c); }
        // ¿son UTF-8 de verdade? (cada byte de continuación, 10xxxxxx; se non, o texto xa era bo:
        // «ó» solta é un carácter, non un byte)
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            const auto b = (unsigned char) bytes[i];
            const int extra = b < 0x80 ? 0 : b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC2 ? 1 : -1;
            if (extra < 0 || i + (size_t) extra >= bytes.size() + (extra == 0 ? 1 : 0)) return t;
            for (int k = 1; k <= extra; ++k) if (((unsigned char) bytes[i + (size_t) k] & 0xC0) != 0x80) return t;
            i += (size_t) extra;
        }
        if (! high) return t;
        const auto u = juce::String::fromUTF8 (bytes.data(), (int) bytes.size());
        return u.isEmpty() ? t : u;
    }

    // opcións: o texto (recomposto) ou, se non, o número de opción
    juce::AudioParameterChoiceAttributes choiceAttrs (const juce::StringArray& choices)
    {
        return juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([choices] (const juce::String& t)
        {
            const auto r = repairUtf8 (t).trim();
            const int i = choices.indexOf (r, true);
            return i >= 0 ? i : juce::jlimit (0, choices.size() - 1, r.getIntValue());
        });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout TuneProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray modes { U ("Escala"), U ("MIDI: escala"), U ("MIDI: nota") };
    StringArray keys, sc;
    for (int i = 0; i < 12; ++i) keys.add (scales::noteName (i));
    for (auto& s : scales::all()) sc.add (U (s.name));

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "modo", 1 }, U ("Modo"), modes, 0, choiceAttrs (modes)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "tonica", 1 }, U ("Tónica"), keys, 0, choiceAttrs (keys)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "escala", 1 }, U ("Escala"), sc, 1, choiceAttrs (sc)));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "nota" + String (i), 1 }, U ("Nota ") + scales::noteName (i), true));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "libre" + String (i), 1 }, U ("Libre ") + scales::noteName (i), false));
    // afinación: igual (12 semitonos iguais), xusta (respecto da tónica) ou personalizada (cents por nota)
    const StringArray afins { U ("Igual"), U ("Xusta"), U ("Personalizada"), U ("MTS-ESP") };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "afinacion", 1 }, U ("Afinación"), afins, 0, choiceAttrs (afins)));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cents" + String (i), 1 }, U ("Cents ") + scales::noteName (i), NormalisableRange<float> (-50.0f, 50.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " c"; })));

    auto msFmt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? String ("0 ms") : String (roundToInt (v)) + " ms"; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "velocidade", 1 }, U ("Velocidade"),
                                                  NormalisableRange<float> (0.0f, 800.0f, 0.0f, 0.3f), 20.0f, msFmt));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "transicion", 1 }, U ("Transición"),
                                                  NormalisableRange<float> (0.0f, 800.0f, 0.0f, 0.3f), 40.0f, msFmt));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "ligar", 1 }, U ("Ligar velocidade e transición"), false,
                                                 AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tolerancia", 1 }, U ("Tolerancia"), NormalisableRange<float> (0.0f, 40.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " c"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "toltempo", 1 }, U ("Tolerancia (tempo)"), NormalisableRange<float> (0.0f, 300.0f, 0.0f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "flexibilidade", 1 }, U ("Flexibilidade"), NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " %"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanizar", 1 }, U ("Humanizar"), NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " %"; })));
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
    const StringArray cals { U ("Directo"), U ("Preciso"), U ("Estudio") };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "calidade", 1 }, U ("Xeito"), cals, 0, choiceAttrs (cals).withAutomatable (false)));
    const StringArray voices { U ("Baixo"), U ("Barítono"), U ("Tenor"), U ("Alto"), U ("Mezzo"), U ("Soprano"), U ("Calquera") };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rango", 1 }, U ("Voz"), voices, 6, choiceAttrs (voices).withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "comparar", 1 }, U ("Comparar (voz orixinal)"), false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, U ("Bypass"), false));

    // aire: alento enriba da voz
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "aire", 1 }, U ("Aire"), NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " %"; })));

    // MIDI
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "bend_rango", 1 }, U ("Rango do pitch bend"), 1, 24, 2,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + " st"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "midi_saida", 1 }, U ("MIDI: notas cantadas"), false));

    // vibrato xerado
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibgen_fondo", 1 }, U ("Vibrato xerado: fondo"), NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.6f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " c"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibgen_velocidade", 1 }, U ("Vibrato xerado: velocidade"), NormalisableRange<float> (2.0f, 10.0f, 0.01f), 5.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibgen_retardo", 1 }, U ("Vibrato xerado: retardo"), NormalisableRange<float> (0.0f, 1500.0f, 1.0f, 0.5f), 300.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibgen_variacion", 1 }, U ("Vibrato xerado: variación"), NormalisableRange<float> (0.0f, 100.0f), 30.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibgen_tremolo", 1 }, U ("Vibrato xerado: trémolo"), NormalisableRange<float> (0.0f, 100.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? U ("non") : String (roundToInt (v)) + " %"; })));

    // armonías
    // (un texto sen díxitos, «non», é apagado)
    auto dbFmt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -47.9f ? U ("non") : String (v, 1) + " dB"; })
                                                 .withValueFromStringFunction ([] (const String& t) { return t.containsAnyOf ("0123456789") ? t.getFloatValue() : -48.0f; });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "principal", 1 }, U ("Voz principal"), NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f, dbFmt));
    const StringArray hmodes { U ("Escala"), U ("Acorde MIDI") };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "armonia_modo", 1 }, U ("Armonías"), hmodes, 0, choiceAttrs (hmodes)));
    for (int h = 0; h < TuneProcessor::numHarmonies; ++h)
    {
        const String id ("harm" + String (h + 1)), nm (U ("Armonía ") + String (h + 1));
        static const int defInt[4] = { 9, 11, 5, 0 };
        static const float defPan[4] = { -35, 35, -65, 65 }, defDet[4] = { 5, -6, 8, -4 }, defDel[4] = { 10, 18, 25, 32 };
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, nm, false));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_intervalo", 1 }, nm + U (": intervalo"), TuneProcessor::intervalNames(), defInt[h], choiceAttrs (TuneProcessor::intervalNames())));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_nivel", 1 }, nm + U (": nivel"), NormalisableRange<float> (-48.0f, 6.0f, 0.1f), -4.0f, dbFmt));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_pan", 1 }, nm + U (": panorama"), NormalisableRange<float> (-100.0f, 100.0f, 1.0f), defPan[h],
            // E35 / D35 (esquerda, dereita) e C no centro; tamén le I/L e R
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const int i = roundToInt (v); return i == 0 ? String ("C") : (i < 0 ? U ("E") + String (-i) : U ("D") + String (i)); })
                                           .withValueFromStringFunction ([] (const String& t)
                                           {
                                               const auto s = t.trim().toUpperCase();
                                               if (s.startsWith ("E") || s.startsWith ("I") || s.startsWith ("L")) return -s.substring (1).getFloatValue();
                                               if (s.startsWith ("D") || s.startsWith ("R")) return s.substring (1).getFloatValue();
                                               return s.startsWith ("C") ? 0.0f : s.getFloatValue();
                                           })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_desafinar", 1 }, nm + U (": desafinar"), NormalisableRange<float> (-25.0f, 25.0f, 0.1f), defDet[h],
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " c"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_atraso", 1 }, nm + U (": atraso"), NormalisableRange<float> (0.0f, 60.0f, 0.1f), defDel[h],
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_formantes", 1 }, nm + U (": formantes"), NormalisableRange<float> (-4.0f, 4.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (v, 1); })));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "formantes", 1 }, U ("Formantes"), NormalisableRange<float> (-7.0f, 7.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (v, 1); })));
    return l;
}

TuneProcessor::TuneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Entrada", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Saída", juce::AudioChannelSet::stereo(), true)
                        .withInput ("Acompañamento", juce::AudioChannelSet::stereo(), false)),   // para detectar a tonalidade coa base
      params (*this, &undo, "BarulloTune", layout())
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
    pFlex = params.getRawParameterValue ("flexibilidade");
    pHuman = params.getRawParameterValue ("humanizar");
    pTrans = params.getRawParameterValue ("transpor");
    pLa = params.getRawParameterValue ("la");
    pMest = params.getRawParameterValue ("mestura");
    pVol = params.getRawParameterValue ("volume");
    pCal = params.getRawParameterValue ("calidade");
    pRango = params.getRawParameterValue ("rango");
    pForm = params.getRawParameterValue ("formantes");
    pComp = params.getRawParameterValue ("comparar");
    pBypass = params.getRawParameterValue ("bypass");
    pPrincipal = params.getRawParameterValue ("principal");
    pVgDepth = params.getRawParameterValue ("vibgen_fondo");
    pVgRate = params.getRawParameterValue ("vibgen_velocidade");
    pVgDelay = params.getRawParameterValue ("vibgen_retardo");
    pVgVar = params.getRawParameterValue ("vibgen_variacion");
    pVgTrem = params.getRawParameterValue ("vibgen_tremolo");
    pAfin = params.getRawParameterValue ("afinacion");
    pBendRange = params.getRawParameterValue ("bend_rango");
    pAir = params.getRawParameterValue ("aire");
    pMidiOut = params.getRawParameterValue ("midi_saida");
    for (int i = 0; i < 12; ++i) pCents[(size_t) i] = params.getRawParameterValue ("cents" + juce::String (i));
    pHarmMode = params.getRawParameterValue ("armonia_modo");
    for (int h = 0; h < numHarmonies; ++h)
    {
        const juce::String id ("harm" + juce::String (h + 1));
        auto& hp = harmP[(size_t) h];
        hp.on = params.getRawParameterValue (id);
        hp.interval = params.getRawParameterValue (id + "_intervalo");
        hp.level = params.getRawParameterValue (id + "_nivel");
        hp.pan = params.getRawParameterValue (id + "_pan");
        hp.detune = params.getRawParameterValue (id + "_desafinar");
        hp.delay = params.getRawParameterValue (id + "_atraso");
        hp.formant = params.getRawParameterValue (id + "_formantes");
    }
    bypassParam = params.getParameter ("bypass");
    for (int i = 0; i < 12; ++i) pNota[(size_t) i] = params.getRawParameterValue ("nota" + juce::String (i));
    for (int i = 0; i < 12; ++i) pLibre[(size_t) i] = params.getRawParameterValue ("libre" + juce::String (i));
    for (auto& h : keyHist) h = 0;
    for (auto& h : chroma) h = 0;

    mts = MTS_RegisterClient();
    engine.prepare (44100.0);
    latencyNow = -1;
    applyEngineSettings();
    setLatencySamples (latencyNow.load());
}

// xeito e rango cambian a latencia: avísase ao DAW
void TuneProcessor::applyEngineSettings()
{
    const int q = juce::jlimit (0, 2, (int) pCal->load()), r = juce::jlimit (0, (int) tune::Engine::numRanges - 1, (int) pRango->load());
    if (q != engine.currentQuality() || r != engine.currentRange() || latencyNow.load() != engine.latency())
    {
        if (latencyNow.load() < 0) engine.configure (q, r);   // ao preparar: de cero
        else engine.switchTo (q, r);                         // en marcha: a análise segue
        latencyNow = engine.latency();
        triggerAsyncUpdate();
    }
}

bool TuneProcessor::engineSettingsPending() const
{
    const int q = juce::jlimit (0, 2, (int) pCal->load()), r = juce::jlimit (0, (int) tune::Engine::numRanges - 1, (int) pRango->load());
    return q != engine.currentQuality() || r != engine.currentRange();
}

void TuneProcessor::handleAsyncUpdate() { setLatencySamples (latencyNow.load()); }

void TuneProcessor::startKeyListening()
{
    for (auto& h : keyHist) h = 0;
    for (auto& h : chroma) h = 0;
    keyHeard = 0; chromaHeard = 0;
    keyListening = true;
}

// A tonalidade sae da voz e, se hai acompañamento na entrada lateral, tamén del (o
// croma da base pesa máis, 70 %: ten os acordes). Abonda con oito segundos de calquera dos dous.
bool TuneProcessor::finishKeyIfReady()
{
    if (! keyListening.load() || juce::jmax (keyHeard.load(), chromaHeard.load()) < keyListenSeconds) return false;
    float hv[12], hc[12], sv = 0, sc = 0;
    for (int i = 0; i < 12; ++i) { hv[i] = keyHist[(size_t) i].load(); hc[i] = chroma[(size_t) i].load(); sv += hv[i]; sc += hc[i]; }
    const float wv = keyHeard.load() > 1.0f && sv > 0 ? (chromaHeard.load() > 1.0f ? 0.3f : 1.0f) : 0.0f;
    const float wc = chromaHeard.load() > 1.0f && sc > 0 ? (keyHeard.load() > 1.0f ? 0.7f : 1.0f) : 0.0f;
    const auto k = scales::detectKeyCombined (hv, wv, hc, wc);
    setParamPlain ("tonica", (float) k.first);
    setParamPlain ("escala", (float) k.second);
    keyListening = false;
    return true;
}

bool TuneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != out) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
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
    bypassSm.reset (sr, 0.02); bypassSm.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    switchGain = 1.0f; switchState = 0; switchStep = (float) (1.0 / (0.005 * sr));
    midiOutNote = -1; midiOutSilence = 0; midiOutHold = (int) (0.04 * sr);
    midiOutPending = -1; midiOutPendingAge = 0; midiOutMinDur = (int) (0.03 * sr);
    airSm.reset (sr, 0.05); airSm.setCurrentAndTargetValue (pAir->load() * 0.01f);
    airEnv = 0; airHp1 = airHp2 = 0; airPrev = 0; airLp = 0;
    airEnvA = (float) (1.0 - std::exp (-1.0 / (0.008 * sr)));
    airHpA = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 2000.0 / sr);
    airLpA = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 9000.0 / sr));
    airGate = 0;
    // croma do acompañamento: cada bin entre 100 Hz e 5 kHz repártese entre as dúas clases
    // de altura máis próximas
    scRing.assign (chromaFft, 0.0f); scPos = 0; scCount = 0;
    fftData.assign (2 * chromaFft, 0.0f);
    hann.resize (chromaFft);
    for (int i = 0; i < chromaFft; ++i) hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / chromaFft));
    binLo.assign (chromaFft / 2, -1); binW.assign (chromaFft / 2, 0.0f);
    for (int k = 1; k < chromaFft / 2; ++k)
    {
        const double f = k * sr / chromaFft;
        if (f < 100.0 || f > 5000.0) continue;
        const double pm = 69.0 + 12.0 * std::log2 (f / 440.0), fl = std::floor (pm);
        binLo[(size_t) k] = ((int) fl % 12 + 12) % 12;
        binW[(size_t) k] = (float) (1.0 - (pm - fl));   // o resto vai á seguinte
    }
    midiOutBuf.ensureSize (2048);
    leadSm.reset (sr, 0.03); leadSm.setCurrentAndTargetValue (leadGainTarget());
    harmSm.reset (sr, 0.03); harmSm.setCurrentAndTargetValue (pComp->load() > 0.5f ? 0.0f : 1.0f);
    for (int h = 0; h < numHarmonies; ++h)
    {
        auto& hs = harmS[(size_t) h];
        hs.line.assign ((size_t) (0.07 * sr) + 4, 0.0f);
        hs.pos = 0;
        hs.gL.reset (sr, 0.03); hs.gR.reset (sr, 0.03); hs.del.reset (sr, 0.05);
        float gl, gr; harmGains (h, gl, gr);
        hs.gL.setCurrentAndTargetValue (gl); hs.gR.setCurrentAndTargetValue (gr);
        hs.del.setCurrentAndTargetValue ((float) (harmP[(size_t) h].delay->load() * 0.001 * sr));
    }
    frameCount = 0; levelAcc = 0;
    fifo.reset();
}

const juce::StringArray& TuneProcessor::intervalNames()
{
    static const juce::StringArray n { U ("8ª abaixo"), U ("7ª abaixo"), U ("6ª abaixo"), U ("5ª abaixo"), U ("4ª abaixo"), U ("3ª abaixo"), U ("2ª abaixo"),
                                       U ("Unísono"), U ("2ª arriba"), U ("3ª arriba"), U ("4ª arriba"), U ("5ª arriba"), U ("6ª arriba"), U ("7ª arriba"), U ("8ª arriba") };
    return n;
}

float TuneProcessor::leadGainTarget() const
{
    const float db = pPrincipal->load();
    return db <= -47.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

// nivel e panorama (potencia constante) dunha armonía
void TuneProcessor::harmGains (int h, float& gl, float& gr) const
{
    const auto& hp = harmP[(size_t) h];
    const float db = hp.level->load();
    const float g = db <= -47.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
    const float a = (juce::jlimit (-100.0f, 100.0f, hp.pan->load()) + 100.0f) / 200.0f * juce::MathConstants<float>::halfPi;
    gl = g * std::cos (a) * juce::MathConstants<float>::sqrt2;   // no centro, cada lado a 0 dB
    gr = g * std::sin (a) * juce::MathConstants<float>::sqrt2;
}

TuneProcessor::~TuneProcessor()
{
    stopRecording();
    cancelPendingUpdate();
    if (mts != nullptr) MTS_DeregisterClient (mts);
}

bool TuneProcessor::mtsConnected() const { return mts != nullptr && MTS_HasMaster (mts); }

juce::String TuneProcessor::mtsScaleName() const
{
    if (! mtsConnected()) return {};
    const char* n = MTS_GetScaleName (mts);
    return n != nullptr ? juce::String::fromUTF8 (n) : juce::String();
}

// MTS-ESP: a frecuencia de cada nota segundo o mestre (ou as mensaxes MIDI de afinación),
// como altura MIDI respecto do La de referencia
void TuneProcessor::applyMtsTuning()
{
    const bool on = juce::jlimit (0, 3, (int) pAfin->load()) == 3 && mts != nullptr && (MTS_HasMaster (mts) || MTS_HasReceivedMTSSysEx (mts));
    engine.useNoteCents = on;
    if (! on) return;
    const double ref = juce::jmax (1.0, (double) pLa->load());
    for (int n = 0; n < 128; ++n)
    {
        const double f = MTS_NoteToFrequency (mts, (char) n, -1);
        engine.noteCents[n] = f > 0 ? juce::jlimit (-1200.0, 1200.0, (69.0 + 12.0 * std::log2 (f / ref) - n) * 100.0) : 0.0;
        engine.noteBlocked[n] = MTS_ShouldFilterNote (mts, (char) n, -1) ? 1 : 0;
    }
}

void TuneProcessor::tuningCents (double* c) const
{
    const int mode = juce::jlimit (0, 3, (int) pAfin->load()), tonic = (int) pTonica->load();
    for (int pc = 0; pc < 12; ++pc)
        c[pc] = mode == 1 ? scales::justCents (pc - tonic) : mode == 2 ? (double) pCents[(size_t) pc]->load() : 0.0;
}

bool TuneProcessor::loadScala (const juce::String& text, juce::String& error)
{
    // formato Scala: liñas que empezan por «!» son comentarios; a primeira outra liña é a
    // descrición, a seguinte o número de notas, e despois unha nota por liña: cents (con
    // punto) ou unha razón (3/2, 5). A última é a oitava (ou o intervalo de repetición).
    juce::StringArray lines;
    lines.addLines (text);
    juce::StringArray rows;
    for (auto& ln : lines) { const auto t = ln.trim(); if (! t.startsWith ("!")) rows.add (t); }
    if (rows.size() < 2) { error = U ("O ficheiro non parece unha escala de Scala."); return false; }
    const int count = rows[1].getIntValue();
    if (count < 1 || rows.size() < 2 + count) { error = U ("Faltan notas na escala."); return false; }
    std::vector<double> cents { 0.0 };
    for (int i = 0; i < count; ++i)
    {
        const auto tok = rows[2 + i].upToFirstOccurrenceOf (" ", false, false).trim();
        double c = 0;
        if (tok.containsChar ('.')) c = tok.getDoubleValue();
        else
        {
            const double num = tok.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double den = tok.containsChar ('/') ? tok.fromFirstOccurrenceOf ("/", false, false).getDoubleValue() : 1.0;
            if (num <= 0 || den <= 0) { error = U ("Nota sen entender: ") + tok; return false; }
            c = 1200.0 * std::log2 (num / den);
        }
        cents.push_back (c);
    }
    const double period = cents.back();
    cents.pop_back();   // a oitava (ou o período) é a repetición da tónica
    if (std::abs (period - 1200.0) > 1.0) { error = U ("A escala non se repite na oitava (") + juce::String (period, 1) + " c)."; return false; }
    const int tonic = (int) pTonica->load();
    uint16_t mask = 0; double cpc[12] {};
    for (double c : cents)
    {
        const int deg = (int) std::lround (c / 100.0);
        const int pc = ((tonic + deg) % 12 + 12) % 12;
        if ((mask >> pc) & 1) { error = U ("Dous graos da escala caen na mesma nota (máis de 12 por oitava?)."); return false; }
        mask |= (uint16_t) (1u << pc);
        cpc[pc] = juce::jlimit (-50.0, 50.0, c - 100.0 * deg);
    }
    for (int i = 0; i < 12; ++i)
    {
        setParamPlain ("nota" + juce::String (i), ((mask >> i) & 1) ? 1.0f : 0.0f);
        setParamPlain ("cents" + juce::String (i), (float) cpc[i]);
    }
    setParamPlain ("escala", (float) scales::custom);
    setParamPlain ("afinacion", 2.0f);
    return true;
}

void TuneProcessor::chromaFrame()
{
    float energy = 0;
    for (int i = 0; i < chromaFft; ++i)
    {
        const float v = scRing[(size_t) ((scPos + i) % chromaFft)];
        fftData[(size_t) i] = v * hann[(size_t) i]; energy += v * v;
    }
    std::fill (fftData.begin() + chromaFft, fftData.end(), 0.0f);
    if (energy / chromaFft < 1.0e-7f) return;   // silencio (−70 dBFS)
    chromaFftObj.performFrequencyOnlyForwardTransform (fftData.data());
    float c[12] {}, sum = 0;
    for (int k = 1; k < chromaFft / 2; ++k)
    {
        const int lo = binLo[(size_t) k];
        if (lo < 0) continue;
        const float e = fftData[(size_t) k] * fftData[(size_t) k];
        c[lo] += e * binW[(size_t) k]; c[(lo + 1) % 12] += e * (1.0f - binW[(size_t) k]);
        sum += e;
    }
    if (sum <= 0) return;
    const float dt = (float) chromaHop / (float) getSampleRate();
    for (int i = 0; i < 12; ++i) chroma[(size_t) i] = chroma[(size_t) i].load() + c[i] / sum * dt;
    chromaHeard = chromaHeard.load() + dt;
}

// ---- A/B e presets propios ----
// Unha foto de todos os parámetros (agás bypass e comparar): <PARAMS><P id=.. v=../>…
juce::ValueTree TuneProcessor::snapshotParams() const
{
    juce::ValueTree t ("PARAMS");
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (rp->paramID == "bypass" || rp->paramID == "comparar") continue;
            juce::ValueTree c ("P");
            c.setProperty ("id", rp->paramID, nullptr);
            c.setProperty ("v", rp->convertFrom0to1 (rp->getValue()), nullptr);
            t.appendChild (c, nullptr);
        }
    return t;
}

void TuneProcessor::applyParams (const juce::ValueTree& snap)
{
    undo.beginNewTransaction();
    for (auto c : snap)
        if (auto* rp = params.getParameter (c.getProperty ("id").toString()))
            setParamPlain (rp->paramID, (float) (double) c.getProperty ("v"));
}

void TuneProcessor::abSwitch()
{
    abState[abSlot] = snapshotParams();
    abSlot = 1 - abSlot;
    if (! abState[abSlot].isValid()) abState[abSlot] = abState[1 - abSlot].createCopy();
    applyParams (abState[abSlot]);
}

void TuneProcessor::abCopy()
{
    abState[1 - abSlot] = snapshotParams();
}

juce::File TuneProcessor::presetFolder() const
{
    if (userFolder != juce::File()) return userFolder;
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Barullo Tune").getChildFile ("Presets");
}

juce::Array<juce::File> TuneProcessor::userPresets() const
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.btpreset");
    files.sort();
    return files;
}

bool TuneProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = presetFolder();
    if (! dir.createDirectory()) return false;
    auto snap = snapshotParams();
    snap.setProperty ("version", stateVersion, nullptr);
    snap.setProperty ("plugin", BARULLO_VERSION, nullptr);
    if (auto xml = snap.createXml()) return xml->writeTo (dir.getChildFile (clean + ".btpreset"));
    return false;
}

bool TuneProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("PARAMS")) return false;
    applyParams (juce::ValueTree::fromXml (*xml));
    return true;
}

// ---- gravar e escoitarse
juce::File TuneProcessor::takesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Barullo Tune");
}

bool TuneProcessor::startRecording (const juce::File& wav)
{
    stopRecording();
    if (! wav.getParentDirectory().createDirectory()) return false;
    wav.deleteFile();
    auto stream = wav.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat fmt;
    const int ch = juce::jmax (1, getMainBusNumOutputChannels());
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (stream.get(), getSampleRate() > 0 ? getSampleRate() : 48000.0, (unsigned) ch, 24, {}, 0));
    if (w == nullptr) return false;
    stream.release();
    writerThread.startThread();
    auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 17);
    {
        const juce::SpinLock::ScopedLockType sl (recLock);
        threadedWriter = std::move (tw);
    }
    lastTakeFile = wav;
    recording = true;
    return true;
}

void TuneProcessor::stopRecording()
{
    recording = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType sl (recLock);
        old = std::move (threadedWriter);
    }
    old.reset();   // fóra do bloqueo: baleira o que quede e pecha o ficheiro
}

bool TuneProcessor::startPlayback (const juce::File& wav)
{
    stopPlayback();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wav));
    if (rd == nullptr || rd->lengthInSamples <= 0) return false;
    // (std::min e non juce::jmin<int64>: con juce_dsp, GCC proba tamén a versión SIMD de long long e non compila)
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (10 * 60 * 48000));
    juce::AudioBuffer<float> fresh ((int) rd->numChannels, len);
    rd->read (&fresh, 0, len, 0, true, true);
    {
        const juce::SpinLock::ScopedLockType sl (recLock);
        std::swap (take, fresh);   // (a vella libérase fóra do bloqueo)
        takePos = 0;
        takeLength = len;
    }
    playing = true;
    return true;
}

TuneProcessor::Practice TuneProcessor::practice() const
{
    Practice p;
    p.seconds = prSeconds.load();
    if (p.seconds <= 0) return p;
    p.within10 = prWithin10.load() / p.seconds;
    p.within25 = prWithin25.load() / p.seconds;
    p.meanAbs = prSumAbs.load() / p.seconds;
    p.meanSigned = prSum.load() / p.seconds;
    for (int i = 0; i < 12; ++i)
    {
        p.noteSeconds[i] = prNoteSec[(size_t) i].load();
        p.noteMean[i] = p.noteSeconds[i] > 0 ? prNoteSum[(size_t) i].load() / p.noteSeconds[i] : 0.0f;
    }
    return p;
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
    //                     velocidade transición vibrato tol tolT cantidade formantes transpor xeito (-1 = non cambia) flexibilidade humanizar
    static const std::vector<Preset> p {
        { "Natural",        "Corrección clara que respecta o vibrato. O punto de partida.",                 20,  40, 100,  0,   0, 100,  0,   0, -1,  0,  0 },
        { "Suave",          "Só arranxa o que se nota: deixa as pequenas desafinacións, os adornos e as notas que a voz busca a propósito.", 60, 120, 100, 12, 60, 85, 0, 0, -1, 40, 40 },
        { "Pop",            "Afinado e limpo, co vibrato algo contido.",                                    8,  25,  70,  0,   0, 100,  0,   0, -1,  0,  0 },
        { "Balada",         "Notas longas centradas que respiran, e vibrato aberto.",                      40,  90, 120,  8,  40, 100,  0,   0, -1,  0, 50 },
        { "Robot",          "O efecto clásico: salta de nota en seco e sen vibrato.",                       0,   0,   0,  0,   0, 100,  0,   0, -1,  0,  0 },
        { "Trap",           "Duro, pero cun pouco de vida nas notas longas.",                               0,  12,  25,  0,   0, 100,  0,   0, -1,  0, 25 },
        { "Directo seguro", "Para cantar en directo sen latencia: corrección discreta.",                   25,  50, 100,  6,  30,  90,  0,   0,  0, 30,  0 },
        { "Xigante",        "Unha oitava abaixo e voz grande (en Estudio).",                                  20,  40, 100,  0,   0, 100, -4, -12,  2,  0,  0 },
        { "Trasno",         "Unha oitava arriba e voz pequena (Estudio).",                                 20,  40, 100,  0,   0, 100,  4,  12,  2,  0,  0 },
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
    setParamPlain ("flexibilidade", pr.flex);
    setParamPlain ("humanizar", pr.human);
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
    f.inMidi = (float) engine.inputMidi();
    f.outMidi = (float) engine.outputMidi();
    f.target = (int16_t) engine.targetNote();
    f.voiced = engine.isVoiced() ? 1 : 0;
    fifo.finishedWrite (1);

    // práctica: cada trama con voz clara, a desviación da voz respecto da súa nota
    if (practiceReset.exchange (false))
    {
        prSeconds = 0; prWithin10 = 0; prWithin25 = 0; prSumAbs = 0; prSum = 0;
        for (auto& v : prNoteSec) v = 0;
        for (auto& v : prNoteSum) v = 0;
    }
    if (engine.isVoiced() && engine.targetNote() >= 0 && engine.clarity() > 0.6)
    {
        const float dt = 256.0f / (float) getSampleRate();
        const float dev = (float) ((engine.inputMidi() - engine.targetNote()) * 100.0);
        if (std::abs (dev) < 60.0f)
        {
            const int pc = engine.targetNote() % 12;
            prSeconds = prSeconds.load() + dt;
            if (std::abs (dev) <= 10.0f) prWithin10 = prWithin10.load() + dt;
            if (std::abs (dev) <= 25.0f) prWithin25 = prWithin25.load() + dt;
            prSumAbs = prSumAbs.load() + std::abs (dev) * dt;
            prSum = prSum.load() + dev * dt;
            prNoteSec[(size_t) pc] = prNoteSec[(size_t) pc].load() + dt;
            prNoteSum[(size_t) pc] = prNoteSum[(size_t) pc].load() + dev * dt;
        }
    }

    if (keyListening.load() && engine.isVoiced() && engine.clarity() > 0.75)
    {
        const float dt = 256.0f / (float) getSampleRate();
        const int pc = ((int) std::lround (engine.inputMidi()) % 12 + 12) % 12;
        keyHist[(size_t) pc] = keyHist[(size_t) pc].load() + dt;
        keyHeard = keyHeard.load() + dt;
    }
}

void TuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) { process (buffer, midi, false); }
void TuneProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) { process (buffer, midi, true); }

// O motor segue traballando en bypass (así, ao volver, non hai que agardar a que
// recoñeza a voz) e a saída funde coa voz orixinal atrasada a latencia.
void TuneProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    // só as canles do bus principal (a entrada lateral, se a hai, vén despois)
    const int numCh = juce::jmin (buffer.getNumChannels(), juce::jmax (1, getMainBusNumOutputChannels())),
              numIn = juce::jmin (buffer.getNumChannels(), getMainBusNumInputChannels());
    const int n = buffer.getNumSamples();

    // cambio de xeito ou de voz: primeiro baixa a saída (abaixo, mostra a mostra)
    if (switchState == 0 && engineSettingsPending()) switchState = -1;
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
    engine.flex = pFlex->load() * 0.01;
    engine.humanize = pHuman->load() * 0.01;
    engine.harmonyMode = juce::jlimit (0, 1, (int) pHarmMode->load());
    tuningCents (engine.tuneCents);
    applyMtsTuning();
    engine.bendRange = pBendRange->load();
    engine.vibDepth = pVgDepth->load();
    engine.vibRate = pVgRate->load();
    engine.vibDelayMs = pVgDelay->load();
    engine.vibVariation = pVgVar->load() * 0.01;
    engine.tremolo = pVgTrem->load() * 0.01;
    bool anyHarm = false;
    for (int h = 0; h < numHarmonies; ++h)
    {
        const auto& hp = harmP[(size_t) h];
        auto& eh = engine.harm[h];
        eh.on = hp.on->load() > 0.5f;
        eh.degrees = juce::jlimit (0, 14, (int) hp.interval->load()) - 7;
        eh.detune = hp.detune->load();
        eh.formant = hp.formant->load();
        anyHarm |= eh.on;
        float gl, gr; harmGains (h, gl, gr);
        harmS[(size_t) h].gL.setTargetValue (gl); harmS[(size_t) h].gR.setTargetValue (gr);
        harmS[(size_t) h].del.setTargetValue ((float) (hp.delay->load() * 0.001 * getSampleRate()));
    }
    leadSm.setTargetValue (leadGainTarget());
    airSm.setTargetValue (pAir->load() * 0.01f);
    harmSm.setTargetValue (pComp->load() > 0.5f ? 0.0f : 1.0f);   // comparar: só a voz orixinal
    engine.freeMask = freeMask();
    engine.transpose = std::round (pTrans->load());
    engine.refA = pLa->load();
    mixSm.setTargetValue (pComp->load() > 0.5f ? 0.0f : pMest->load() * 0.01f);   // comparar: a voz sen tocar
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (pVol->load()));
    bypassSm.setTargetValue (hostBypass || pBypass->load() > 0.5f ? 1.0f : 0.0f);

    const bool midiOutOn = pMidiOut->load() > 0.5f;
    // acompañamento (entrada lateral), só mentres se escoita a tonalidade
    const bool sideOn = keyListening.load() && getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    if (sideOn)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        const int scn = sc.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            float v = 0; for (int c = 0; c < scn; ++c) v += sc.getReadPointer (c)[i];
            scRing[(size_t) scPos] = v / (float) juce::jmax (1, scn);
            scPos = (scPos + 1) % chromaFft;
            if (++scCount >= chromaHop) { scCount = 0; chromaFrame(); }
        }
    }
    midiOutBuf.clear();
    auto it = midi.cbegin();
    const auto end = midi.cend();
    const int dsz = (int) dry[0].size();
    int lat = juce::jmin (dsz - 1, engine.latency());

    for (int i = 0; i < n; ++i)
    {
        for (; it != end && (*it).samplePosition <= i; ++it)
        {
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getChannel());
            else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
            else if (m.isPitchWheel()) engine.setPitchBend (m.getPitchWheelValue(), m.getChannel());
            else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
            else if (m.isSysEx() && mts != nullptr) MTS_ParseMIDIDataU (mts, m.getRawData(), m.getRawDataSize());   // afinación MIDI (MTS)
        }
        // estéreo de verdade: o ton detéctase no centro e as dúas canles corríxense igual
        const int ech = numIn >= 2 && numCh >= 2 ? 2 : 1;
        float x[2] = { 0, 0 }, wet[2] = { 0, 0 };
        if (ech == 2) { x[0] = buffer.getReadPointer (0)[i]; x[1] = buffer.getReadPointer (1)[i]; }
        else if (numIn > 0) x[0] = buffer.getReadPointer (0)[i];
        for (auto& v : x) if (! std::isfinite (v)) v = 0;

        float hv[numHarmonies] = {};
        engine.processFrame (x, wet, ech, anyHarm ? hv : nullptr);
        // armonías: atraso, nivel e panorama de cada unha
        float harmC[2] = { 0, 0 };
        const float hm = harmSm.getNextValue();
        for (int h = 0; h < numHarmonies; ++h)
        {
            auto& hs = harmS[(size_t) h];
            const float gl = hs.gL.getNextValue(), gr = hs.gR.getNextValue(), dl = hs.del.getNextValue();
            if (! anyHarm || ! engine.harm[h].on) continue;
            const int L = (int) hs.line.size();
            hs.line[(size_t) hs.pos] = hv[h];
            double rpos = hs.pos - (double) dl; while (rpos < 0) rpos += L;
            const int i0 = (int) rpos, i1 = (i0 + 1) % L;
            const float fr = (float) (rpos - i0);
            const float v = (hs.line[(size_t) i0] + (hs.line[(size_t) i1] - hs.line[(size_t) i0]) * fr) * hm;
            if (++hs.pos >= L) hs.pos = 0;
            if (ech == 2) { harmC[0] += v * gl; harmC[1] += v * gr; }
            else harmC[0] += v * std::sqrt (gl * gl + gr * gr);   // mono: sen panorama
        }
        int rd = dryPos - lat; if (rd < 0) rd += dsz;
        const float mix = mixSm.getNextValue(), g = gainSm.getNextValue(), byp = bypassSm.getNextValue(), lead = leadSm.getNextValue();
        // aire: ruído por riba de ~2 kHz que segue a envolvente da voz afinada, só con voz
        const float airAmt = airSm.getNextValue();
        if (airAmt > 0.0f)
        {
            const float w0 = ech == 2 ? 0.5f * (wet[0] + wet[1]) : wet[0];
            airEnv += (std::abs (w0) - airEnv) * airEnvA;
            airGate += ((engine.isVoiced() ? 1.0f : 0.0f) - airGate) * airEnvA;
            airRng = airRng * 1664525u + 1013904223u;
            const float nz = (float) (int32_t) airRng * (1.0f / 2147483648.0f);
            // paso alto de 2.ª orde (dous polos) e un paso baixo suave a 9 kHz
            const float h1 = airHpA * (airHp1 + nz - airPrev); airPrev = nz; airHp1 = h1;
            const float h2 = airHpA * (airHp2 + h1 - airHp1Prev); airHp1Prev = h1; airHp2 = h2;
            airLp += (h2 - airLp) * airLpA;
            const float a = airLp * airEnv * airGate * airAmt * 1.2f;   // (vai na voz afinada: a mestura aplícase despois)
            for (int c = 0; c < ech; ++c) wet[c] += a;
        }
        float peakIn = 0, peakOut = 0;
        for (int c = 0; c < ech; ++c)
        {
            dry[(size_t) c][(size_t) dryPos] = x[c];
            const float dx = dry[(size_t) c][(size_t) rd];
            float y = ((dx + (wet[c] - dx) * mix) * lead + harmC[c]) * g;
            if (byp > 0.0f) y = byp >= 1.0f ? dx : y + (dx - y) * byp;
            y *= switchGain;
            buffer.getWritePointer (c)[i] = y;
            peakIn = juce::jmax (peakIn, std::abs (x[c])); peakOut = juce::jmax (peakOut, std::abs (y));
        }
        if (ech == 1) for (int c = 1; c < numCh; ++c) buffer.getWritePointer (c)[i] = buffer.getReadPointer (0)[i];
        if (++dryPos >= dsz) dryPos = 0;
        if (switchState < 0 && (switchGain -= switchStep) <= 0.0f)
        {
            switchGain = 0.0f; switchState = 1;
            applyEngineSettings();
            lat = juce::jmin (dsz - 1, engine.latency());
        }
        else if (switchState > 0 && (switchGain += switchStep) >= 1.0f) { switchGain = 1.0f; switchState = 0; }

        levelAcc = juce::jmax (levelAcc * 0.9995f, peakIn);
        // MIDI de saída: a nota á que vai a voz (nota nova = apagar a anterior e acender esta;
        // máis de 40 ms sen voz = apagar), coa velocidade segundo o nivel da voz. Unha nota
        // ten que durar 30 ms para saír: así as notas de paso dun glisando non saen
        if (midiOutOn)
        {
            const int t = engine.isVoiced() ? engine.targetNote() : -1;
            if (t >= 0) midiOutSilence = 0; else ++midiOutSilence;
            // (os 30 ms só contan mentres a voz está de verdade na nota, a menos de medio semitono)
            if (t != midiOutPending) { midiOutPending = t; midiOutPendingAge = 0; }
            else if (t >= 0 && std::abs (engine.inputMidi() - t) < 0.5) ++midiOutPendingAge;
            if (t >= 0 && t != midiOutNote && midiOutPendingAge >= midiOutMinDur)
            {
                if (midiOutNote >= 0) midiOutBuf.addEvent (juce::MidiMessage::noteOff (1, midiOutNote), i);
                const float db = juce::Decibels::gainToDecibels (levelAcc, -60.0f);
                const int vel = juce::jlimit (1, 127, juce::roundToInt (30.0f + (db + 40.0f) / 40.0f * 97.0f));
                midiOutBuf.addEvent (juce::MidiMessage::noteOn (1, t, (juce::uint8) vel), i);
                midiOutNote = t;
            }
            else if (t < 0 && midiOutNote >= 0 && midiOutSilence > midiOutHold)
            {
                midiOutBuf.addEvent (juce::MidiMessage::noteOff (1, midiOutNote), i);
                midiOutNote = -1;
            }
        }
        outAcc = juce::jmax (outAcc * 0.9995f, peakOut);
        if (++frameCount >= 256) { frameCount = 0; pushFrame(); }
    }
    for (; it != end; ++it)
    {
        const auto m = (*it).getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getChannel());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isPitchWheel()) engine.setPitchBend (m.getPitchWheelValue(), m.getChannel());
    }
    // escoitar a toma (en bucle) e gravar a saída
    if (playing.load() || recording.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (recLock);
        if (tl.isLocked())
        {
            if (playing.load() && takeLength.load() > 0 && take.getNumChannels() > 0)
            {
                const int len = takeLength.load(), tch = take.getNumChannels();
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] += take.getSample (juce::jmin (c, tch - 1), takePos);
                    if (++takePos >= len) takePos = 0;
                }
            }
            if (recording.load() && threadedWriter != nullptr)
                threadedWriter->write (buffer.getArrayOfReadPointers(), n);   // (se o disco non dá, pérdese un anaco: o audio non agarda)
        }
    }

    // MIDI de saída: as notas cantadas substitúen o que entrou
    if (midiOutOn) midi.swapWith (midiOutBuf);
    else if (midiOutNote >= 0) { midi.addEvent (juce::MidiMessage::noteOff (1, midiOutNote), 0); midiOutNote = -1; }

    nowIn = (float) engine.inputMidi(); nowOut = (float) engine.outputMidi();
    nowHz = (float) engine.detectedHz(); nowCents = (float) engine.correctionCents();
    nowTarget = engine.targetNote(); nowVoiced = engine.isVoiced();
    nowHeld = engine.heldPitchClasses();
    inLevel = levelAcc;
    outLevel = outAcc;
}

void TuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    params.state.setProperty ("version", stateVersion, nullptr);
    params.state.setProperty ("plugin", BARULLO_VERSION, nullptr);
    params.state.setProperty ("preset", currentPreset, nullptr);
    params.state.setProperty ("presetName", juce::String::fromUTF8 (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, currentPreset)].name), nullptr);
    params.state.setProperty ("ui", uiScale.load(), nullptr);
    params.state.setProperty ("tab", uiTab.load(), nullptr);
    auto st = params.copyState();
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

// Estados de versións anteriores. Versión 0: sen número de versión (Barullo Tune 0.1).
// Cando cambie un parámetro (nome, rango, opcións), súbese stateVersion e tradúcese aquí.
void TuneProcessor::migrateState (juce::ValueTree& st, int fromVersion)
{
    if (fromVersion < 1)
    {
        // 0 → 1: só se engadiu o bypass; os parámetros que faltan póñense ao valor de fábrica (abaixo)
    }
    if (fromVersion < 2)
    {
        // 1 → 2: escalas novas antes de «Personalizada» (que pasa do 9 ao 19)
        auto e = st.getChildWithProperty ("id", "escala");
        if (e.isValid() && (int) std::lround ((double) e.getProperty ("value", 0.0)) == scales::customV1)
            e.setProperty ("value", (double) scales::custom, nullptr);
    }
}

void TuneProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    const int from = (int) st.getProperty ("version", 0);
    migrateState (st, from);
    // un estado vello non trae os parámetros novos: van co valor de fábrica, non co que tivese esta instancia
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! st.getChildWithProperty ("id", wp->paramID).isValid())
            {
                juce::ValueTree child ("PARAM");
                child.setProperty ("id", wp->paramID, nullptr);
                child.setProperty ("value", wp->convertFrom0to1 (wp->getDefaultValue()), nullptr);
                st.appendChild (child, nullptr);
            }
    params.replaceState (st);
    // o preset búscase polo nome (se cambia a orde da lista, segue sendo o mesmo)
    int pr = (int) st.getProperty ("preset", 0);
    const auto name = st.getProperty ("presetName").toString();
    for (int i = 0; i < (int) presets().size() && name.isNotEmpty(); ++i)
        if (juce::String::fromUTF8 (presets()[(size_t) i].name) == name || getProgramName (i) == name) pr = i;
    currentPreset = juce::jlimit (0, (int) presets().size() - 1, pr);
    uiScale = juce::jlimit (0.75f, 1.5f, (float) (double) st.getProperty ("ui", 1.0));
    uiTab = juce::jlimit (0, 3, (int) st.getProperty ("tab", 0));
    // os valores cambiaron todos de golpe: que o DAW os volva ler (en CLAP, «rescan»)
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
}

juce::AudioProcessorEditor* TuneProcessor::createEditor() { return new TuneEditor (*this); }

#ifndef BARULLO_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TuneProcessor(); }
#endif
