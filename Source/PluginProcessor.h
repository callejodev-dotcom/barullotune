#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <escoita_nucleo/escoita_nucleo.h>
#include <array>
#include <atomic>
#include "tune/Engine.h"
#include "Edicion.h"

namespace scales
{
    // intervalos sobre a tónica (bit i = semitono i)
    struct Scale { const char* name; uint16_t mask; };
    inline const std::array<Scale, 20>& all()
    {
        static const std::array<Scale, 20> s { {
            { "Cromática",           0x0FFF },
            { "Maior",               0b101010110101 },
            { "Menor natural",       0b010110101101 },
            { "Menor harmónica",     0b100110101101 },
            { "Pentatónica maior",   0b001010010101 },
            { "Pentatónica menor",   0b010010101001 },
            { "Blues",               0b010011101001 },
            { "Dórica",              0b011010101101 },
            { "Mixolidia",           0b011010110101 },
            { "Menor melódica",      0b101010101101 },
            { "Frixia",              0b010110101011 },
            { "Lidia",               0b101011010101 },
            { "Locria",              0b010101101011 },
            { "Frixia dominante",    0b010110110011 },   // a do flamenco
            { "Dobre harmónica",     0b100110110011 },
            { "Tons enteiros",       0b010101010101 },
            { "Disminuída",          0b101101101101 },   // ton-semitono
            { "Hirajoshi",           0b000110001101 },
            { "Blues maior",         0b001010011101 },
            { "Personalizada",       0 },
        } };
        return s;
    }
    constexpr int custom = 19;
    constexpr int customV1 = 9;   // «Personalizada» nos estados da versión 1 (antes das escalas novas)

    // afinación xusta (5-limit) respecto da tónica: cents de cada grao cromático
    inline double justCents (int degree)
    {
        static const double c[12] = { 0.0, 11.73, 3.91, 15.64, -13.69, -1.96, -9.78, 1.96, 13.69, -15.64, 17.60, -11.73 };
        return c[((degree % 12) + 12) % 12];
    }
    inline juce::String noteName (int pc)
    {
        static const char* n[12] = { "Do", "Do♯", "Re", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si" };
        return juce::String::fromUTF8 (n[((pc % 12) + 12) % 12]);
    }
    // nome con oitava (Do central = Do3, coma nos outros Barullos)
    inline juce::String noteWithOctave (int midi) { return noteName (midi) + juce::String (midi / 12 - 2); }
    inline uint16_t rotate (uint16_t rel, int key) { key = ((key % 12) + 12) % 12; return (uint16_t) (((rel << key) | (rel >> (12 - key))) & 0x0FFF); }

    // tonalidade a partir dun histograma de clases de altura (perfís de Krumhansl–Kessler).
    // Devolve tónica (0–11) e escala (1 = maior, 2 = menor natural).
    // Dous histogramas (a voz e o acompañamento): cada un correlaciónase cos perfís por
    // separado e súmanse as puntuacións cos pesos. (Sumar os histogramas facía que unha
    // nota soa e longa da voz mandase: saía de tónica.)
    std::pair<int, int> detectKey (const float* hist);
    inline std::pair<int, int> detectKeyCombined (const float* a, float wa, const float* b, float wb)
    {
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [] (const float* hist, const double* prof, int k)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += hist[(i + k) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = hist[(i + k) % 12] - mx, y = prof[i] - my;
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        int bk = 0, bs = 1; double best = -1e9;
        for (int k = 0; k < 12; ++k)
            for (int sc = 1; sc <= 2; ++sc)
            {
                const double* prof = sc == 1 ? maj : mnr;
                const double v = (wa > 0 ? wa * corr (a, prof, k) : 0.0) + (wb > 0 ? wb * corr (b, prof, k) : 0.0);
                if (v > best) { best = v; bk = k; bs = sc; }
            }
        return { bk, bs };
    }

    inline std::pair<int, int> detectKey (const float* hist)
    {
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int k)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += hist[(i + k) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = hist[(i + k) % 12] - mx, b = prof[i] - my;
                sxy += a * b; sxx += a * a; syy += b * b;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        int bk = 0, bs = 1; double best = -2;
        for (int k = 0; k < 12; ++k)
        {
            const double a = corr (maj, k), b = corr (mnr, k);
            if (a > best) { best = a; bk = k; bs = 1; }
            if (b > best) { best = b; bk = k; bs = 2; }
        }
        return { bk, bs };
    }
}

class TuneProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    TuneProcessor();
    ~TuneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // bypass do DAW: a voz orixinal sae atrasada a mesma latencia (o DAW non a despraza) e cun fundido
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Barullo Tune"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // as notas cantadas (se se pide)
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // presets de fábrica: só tocan o carácter da corrección (non a tonalidade nin a voz)
    struct Preset { const char* name; const char* description; float vel, transi, vib, tol, tolT, cant, form, trans; int quality; float flex, human; };
    static const std::vector<Preset>& presets();
    int getNumPrograms() override { return (int) presets().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    // versión do formato do estado: súbese cada vez que cambian os parámetros, e
    // migrateState() leva os estados vellos ao formato novo
    static constexpr int stateVersion = 2;
    static void migrateState (juce::ValueTree& state, int fromVersion);

    juce::UndoManager undo;   // desfacer e refacer os cambios de parámetros (declárase antes de params)
    juce::AudioProcessorValueTreeState params;

    // A/B (todo agás o bypass e comparar), presets propios (.btpreset en Documentos/Barullo
    // Tune/Presets) e desfacer: do núcleo común
    escoita::Estado estado { params, &undo, opcionsEstado() };

    // armonías
    static constexpr int numHarmonies = tune::Settings::maxHarmonies;
    static const juce::StringArray& intervalNames();   // 8ª abaixo … unísono … 8ª arriba (graos −7..+7)

    // máscara de notas permitidas (bit = clase de altura absoluta, 0 = Do)
    uint16_t effectiveMask() const;
    // a interface pulsa unha tecla da escala: pasa a «Personalizada» se fai falta
    void toggleScaleNote (int pc);
    // notas libres: a voz pode ir a elas e non se corrixe
    uint16_t freeMask() const;
    void toggleFreeNote (int pc);
    void setParamPlain (const juce::String& id, float plain);

    // afinación de cada clase de altura (cents, absoluta: 0 = Do), segundo «afinacion»
    void tuningCents (double* cents12) const;
    // escala de Scala (.scl): pon a escala Personalizada (as notas máis próximas a cada grao,
    // desde a tónica) e a afinación Personalizada cos cents de cada unha. Escalas de ata 12
    // graos por oitava; se dous graos caen na mesma nota, erro.
    bool loadScala (const juce::String& text, juce::String& error);
    // MTS-ESP: ¿hai un mestre?, e o nome da súa escala
    bool mtsConnected() const;
    juce::String mtsScaleName() const;

    // detectar a tonalidade escoitando a voz uns segundos
    static constexpr float keyListenSeconds = 8.0f;
    void startKeyListening();
    void stopKeyListening() { keyListening = false; }
    bool isKeyListening() const { return keyListening.load(); }
    float keyProgress() const { return juce::jlimit (0.0f, 1.0f, juce::jmax (keyHeard.load(), chromaHeard.load()) / keyListenSeconds); }
    bool finishKeyIfReady();   // fío da interface: aplica a tonalidade cando xa escoitou abondo
    void flushLatencyUpdate() { handleUpdateNowIfNeeded(); }   // para as probas

    // práctica: como afina quen canta (a voz de entrada respecto da nota á que vai)
    struct Practice
    {
        float seconds = 0, within10 = 0, within25 = 0, meanAbs = 0, meanSigned = 0;
        float noteSeconds[12] {}, noteMean[12] {};   // por nota (clase de altura): tempo e desviación media (cents, con signo)
    };
    Practice practice() const;
    void resetPractice() { practiceReset = true; }

    // gravar e escoitarse (pensado para a aplicación independente; nun DAW, grava o DAW).
    // Gravar: a saída vai a un WAV (nun fío aparte). Escoitar: a última toma soa en bucle
    // mesturada na saída.
    bool startRecording (const juce::File& wav);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    juce::File lastTake() const { return lastTakeFile; }
    bool startPlayback (const juce::File& wav);   // en bucle ata stopPlayback
    void stopPlayback() { playing = false; }
    bool isPlaying() const { return playing.load(); }
    static juce::File takesFolder();

    // latencia actual en ms (para amosala)
    // o atraso de verdade (en Directo declárase 0 ao DAW, pero quedan 0,25 ms)
    float latencyMs() const { return (float) (1000.0 * realLatency.load() / juce::jmax (1.0, getSampleRate())); }
    int realLatencySamples() const { return realLatency.load(); }

    // edición: o ton capturado sobre a liña de tempo do DAW e as notas editadas á man
    Edicion edicion;
    void edicionTick();   // no fío da mensaxe (cada 100 ms; nas probas, á man)
    // as notas ás que vai a corrección en directo (para o destino das notas sen editar)
    Edicion::Permitida notaPermitida() const
    {
        return [this] (int n) { const int pc = ((n % 12) + 12) % 12; return (((effectiveMask() | freeMask()) >> pc) & 1) != 0; };
    }

    // para a interface: historia do ton (unha trama cada 256 mostras)
    struct Frame { float inMidi, outMidi; int16_t target; uint8_t voiced; };
    static constexpr int fifoSize = 2048;
    juce::AbstractFifo fifo { fifoSize };
    std::array<Frame, fifoSize> frames {};
    std::atomic<float> nowIn { 0 }, nowOut { 0 }, nowHz { 0 }, nowCents { 0 };
    std::atomic<int> nowTarget { -1 }, nowHeld { 0 };
    std::atomic<bool> nowVoiced { false };
    std::atomic<float> inLevel { 0 }, outLevel { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void pushFrame();
    void handleAsyncUpdate() override;
    void applyEngineSettings();
    bool engineSettingsPending() const;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass);
    float leadGainTarget() const;
    void harmGains (int h, float& gl, float& gr) const;
    void applyMtsTuning();
    struct MTSClient* mts = nullptr;

    tune::Engine engine;
    std::array<std::vector<float>, 2> dry;
    int dryPos = 0;
    juce::SmoothedValue<float> mixSm, gainSm, bypassSm;
    // cambio de xeito ou de voz en marcha: a saída baixa en 5 ms, cámbiase e volve subir
    float switchGain = 1.0f, switchStep = 0.004f;
    int switchState = 0;   // 0 nada, −1 baixando, +1 subindo
    int frameCount = 0;
    float levelAcc = 0, outAcc = 0;
    std::atomic<int> latencyNow { 0 }, realLatency { 0 };
    int currentPreset = 0;
    static escoita::Estado::Opcions opcionsEstado();
public:
    std::atomic<float> uiScale { 1.0f };   // tamaño da xanela (gárdase co proxecto)
    std::atomic<int> uiTab { 0 };          // a pestana aberta (tamén)
private:

    std::atomic<bool> keyListening { false };
    std::atomic<float> keyHeard { 0 };
    std::array<std::atomic<float>, 12> keyHist {};
    // gravación e escoita
    juce::TimeSliceThread writerThread { "Barullo Tune: gravar" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    // o fío de audio só _intenta_ coller isto (nunca agarda); a interface cólleo para
    // cambiar o escritor ou a toma, así nunca se destrúen mentres se usan
    juce::SpinLock recLock;
    std::atomic<bool> recording { false }, playing { false };
    juce::File lastTakeFile;
    juce::AudioBuffer<float> take;   // a toma que se escoita (cárgase fóra do fío de audio)
    std::atomic<int> takeLength { 0 };
    int takePos = 0;
    // práctica (escribe o fío de audio en pushFrame; le a interface)
    std::atomic<bool> practiceReset { false };
    std::atomic<float> prSeconds { 0 }, prWithin10 { 0 }, prWithin25 { 0 }, prSumAbs { 0 }, prSum { 0 };
    std::array<std::atomic<float>, 12> prNoteSec {}, prNoteSum {};
    // croma do acompañamento (entrada lateral)
    // FFT de 16384 (barras de 2,9 Hz a 48 kHz: medio semitono xa aos 100 Hz) cada 4096 mostras
    static constexpr int chromaOrder = 14, chromaFft = 1 << chromaOrder, chromaHop = chromaFft / 4;
    void chromaFrame();
    juce::dsp::FFT chromaFftObj { chromaOrder };
    std::vector<float> scRing, fftData, hann, binW;
    std::vector<int> binLo;
    int scPos = 0, scCount = 0;
    std::array<std::atomic<float>, 12> chroma {};
    std::atomic<float> chromaHeard { 0 };

    std::atomic<float>* pCapturar = nullptr;
    // edición: a que chegou nun estado desde fóra do fío da mensaxe, á espera de cargarse
    void cargarEdicion (const juce::ValueTree&);
    juce::CriticalSection edicionPendenteLock;
    juce::ValueTree edicionPendente;
    uint16_t edicionMascara = 0;
    struct ReloxoEdicion : juce::Timer
    {
        explicit ReloxoEdicion (TuneProcessor& x) : p (x) {}
        void timerCallback() override { p.edicionTick(); }
        TuneProcessor& p;
    } reloxoEdicion { *this };

    std::atomic<float>* pModo, *pTonica, *pEscala, *pVel, *pCant, *pTrans, *pLa, *pMest, *pVol, *pCal, *pRango, *pForm, *pComp;
    std::atomic<float>* pTransicion, *pTol, *pTolT, *pVib, *pBypass, *pFlex, *pHuman, *pPrincipal, *pHarmMode;
    std::atomic<float>* pVgDepth, *pVgRate, *pVgDelay, *pVgVar, *pVgTrem, *pAfin, *pBendRange, *pMidiOut;
    juce::MidiBuffer midiOutBuf;
    std::atomic<float>* pAir = nullptr;
    juce::SmoothedValue<float> airSm;
    float airEnv = 0, airGate = 0, airHp1 = 0, airHp2 = 0, airHp1Prev = 0, airPrev = 0, airLp = 0, airEnvA = 0.003f, airHpA = 0.77f, airLpA = 0.7f;
    uint32_t airRng = 22222u;
    int midiOutNote = -1, midiOutSilence = 0, midiOutHold = 1920, midiOutPending = -1, midiOutPendingAge = 0, midiOutMinDur = 1440;
    std::array<std::atomic<float>*, 12> pCents {};
    struct HarmParams { std::atomic<float>* on, *interval, *level, *pan, *detune, *delay, *formant; };
    std::array<HarmParams, numHarmonies> harmP {};
    struct HarmState { std::vector<float> line; int pos = 0; juce::SmoothedValue<float> gL, gR, del; };
    std::array<HarmState, numHarmonies> harmS;
    juce::SmoothedValue<float> leadSm, harmSm;
    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::array<std::atomic<float>*, 12> pNota {}, pLibre {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneProcessor)
};
