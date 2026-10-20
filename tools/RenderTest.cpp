// Proba sen interface: unha voz sintética desafinada pasa polo procesador e mídese
// a afinación da saída. Tamén: independencia do tamaño de bloque, mono, MIDI,
// estado gardado e (con --snapshot) unha captura da interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static const double SR = 48000.0;

// ---------------------------------------------------------------- voz sintética
struct Vowel { double f[3], b[3], g[3]; };
static const Vowel VA { {730, 1090, 2440}, {90, 110, 160}, {1.0, 0.5, 0.25} };
static const Vowel VE { {530, 1840, 2480}, {80, 120, 160}, {1.0, 0.45, 0.3} };
static const Vowel VO { {570, 840, 2410},  {80, 100, 160}, {1.0, 0.6, 0.15} };
static double envAt (const Vowel& v, double hz)
{
    double s = 0;
    for (int i = 0; i < 3; ++i) { const double d = (hz - v.f[i]) / (v.b[i] * 0.5); s += v.g[i] / std::sqrt (1.0 + d * d); }
    return s;
}
struct Note { double midi, cents, dur; int vowel; bool sss; };

static std::vector<Note> melody()
{
    return { {60, +35, 0.7, 0, false}, {62, -38, 0.5, 1, false}, {64, +28, 0.5, 2, true}, {65, -30, 0.6, 0, false},
             {67, +42, 0.9, 0, false}, {65, -20, 0.4, 1, false}, {64, +33, 0.4, 2, false}, {62, -42, 0.4, 0, true},
             {60, +15, 1.2, 0, false}, {55, -35, 0.6, 2, false}, {57, +40, 0.6, 0, false}, {48, -30, 0.8, 0, false} };
}

static std::vector<float> makeVoice (std::vector<double>& truth)
{
    const auto notes = melody();
    std::vector<float> x;
    std::mt19937 rng (7);
    std::normal_distribution<double> g (0, 1);
    double phase = 0, prevMidi = notes[0].midi + notes[0].cents / 100.0, jit = 0, hp = 0, prev = 0;
    for (const auto& nt : notes)
    {
        const Vowel& V = nt.vowel == 0 ? VA : nt.vowel == 1 ? VE : VO;
        const int N = (int) (nt.dur * SR), glide = (int) (0.06 * SR), sN = nt.sss ? (int) (0.12 * SR) : 0;
        const double goal = nt.midi + nt.cents / 100.0;
        for (int i = 0; i < N; ++i)
        {
            double m = goal;
            if (i < glide) m = prevMidi + (goal - prevMidi) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / glide));
            const double t = (double) i / SR;
            if (t > 0.25) m += 0.22 * std::min (1.0, (t - 0.25) / 0.3) * std::sin (2 * juce::MathConstants<double>::pi * 5.4 * t);
            jit = 0.999 * jit + 0.001 * g (rng);
            m += jit;
            truth.push_back (i < sN ? -1 : m);
            const double f0 = 440.0 * std::pow (2.0, (m - 69) / 12.0);
            phase += f0 / SR; if (phase > 1e6) phase -= 1e6;
            double s = 0;
            if (i >= sN)
            {
                const int H = (int) (0.45 * SR / f0);
                for (int h = 1; h <= H; ++h) s += envAt (V, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * phase);
                s *= 0.12 * std::min (1.0, (i - sN) / (0.03 * SR)) * std::min (1.0, (N - i) / (0.02 * SR));
            }
            else
            {
                const double w = g (rng); hp = 0.6 * (hp + w - prev); prev = w;
                s = 0.05 * hp * std::sin (juce::MathConstants<double>::pi * i / sN);
            }
            x.push_back ((float) s);
        }
        prevMidi = goal;
    }
    return x;
}

// YIN fino para medir a saída
static double measure (const float* x, int64_t len, int64_t c)
{
    const int W = 1600, T = 700;
    if (c - W / 2 < 0 || c + W / 2 + T >= len) return -1;
    const int64_t s0 = c - W / 2;
    std::vector<double> d (T + 2);
    double run = 0, en = 0; d[0] = 1;
    for (int j = 0; j < W; ++j) en += x[s0 + j] * x[s0 + j];
    if (en / W < 1e-6) return -1;
    for (int t = 1; t <= T; ++t)
    {
        double s = 0;
        for (int j = 0; j < W; ++j) { const double e = x[s0 + j] - x[s0 + j + t]; s += e * e; }
        run += s; d[(size_t) t] = s * t / run;
    }
    int b = -1;
    for (int t = 40; t < T; ++t) if (d[(size_t) t] < 0.1) { while (t + 1 < T && d[(size_t) t + 1] < d[(size_t) t]) ++t; b = t; break; }
    if (b < 0) return -1;
    double tau = b; const double den = d[(size_t) b - 1] - 2 * d[(size_t) b] + d[(size_t) b + 1];
    if (den > 0) tau += 0.5 * (d[(size_t) b - 1] - d[(size_t) b + 1]) / den;
    return 69 + 12 * std::log2 (SR / tau / 440.0);
}

struct Stats { double inC = 0, outC = 0, maxC = 0; int frames = 0, wrong = 0; };

// desafinación respecto da nota agardada (a da melodía + desprazamento)
static Stats evaluate (const juce::AudioBuffer<float>& y, int lat, const std::vector<double>& truth, int shift)
{
    Stats s;
    const float* p = y.getReadPointer (0);
    const auto notes = melody();
    std::vector<int> want (truth.size());
    { size_t k = 0; for (auto& nt : notes) for (int i = 0; i < (int) (nt.dur * SR); ++i) want[k++] = (int) nt.midi + shift; }
    for (int64_t c = 4800; c + lat < (int64_t) y.getNumSamples() - 4800 && c < (int64_t) truth.size(); c += 480)
    {
        if (truth[(size_t) c] < 0) continue;
        bool stable = true;
        for (int64_t k = c - 2400; k <= c + 2400; k += 240)
            if (k < 0 || k >= (int64_t) truth.size() || truth[(size_t) k] < 0 || std::abs (truth[(size_t) k] - truth[(size_t) c]) > 0.4) stable = false;
        if (! stable) continue;
        const double mo = measure (p, y.getNumSamples(), c + lat);
        if (mo < 0) continue;
        const double w = want[(size_t) c];
        s.inC += std::abs (truth[(size_t) c] + shift - w) * 100;
        const double e = std::abs (mo - w) * 100;
        if (e > 50) { s.wrong++; continue; }
        s.outC += e; s.maxC = std::max (s.maxC, e); s.frames++;
    }
    if (s.frames > 0) { s.inC /= (s.frames + s.wrong); s.outC /= s.frames; }
    return s;
}

static void setP (TuneProcessor& p, const char* id, float plain)
{
    auto* r = p.params.getParameter (id);
    r->setValueNotifyingHost (r->convertTo0to1 (plain));
}

static juce::AudioBuffer<float> run (const std::vector<float>& voice, int block, int channels,
                                     std::function<void (TuneProcessor&)> setup, int& lat,
                                     std::vector<std::pair<int, int>> midiEv = {})
{
    TuneProcessor p;
    p.setPlayConfigDetails (channels, channels, SR, block);
    setup (p);
    p.prepareToPlay (SR, block);
    lat = p.getLatencySamples();
    const int total = (int) voice.size() + lat;
    juce::AudioBuffer<float> out (channels, total), buf (channels, block);
    size_t k = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (channels, n, false, false, true);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample (c, i, pos + i < (int) voice.size() ? voice[(size_t) (pos + i)] : 0.0f);
        juce::MidiBuffer midi;
        while (k < midiEv.size() && midiEv[k].first < pos + n)
        {
            const int at = juce::jmax (0, midiEv[k].first - pos);
            const int note = midiEv[k].second;
            midi.addEvent (note > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, -note), at);
            ++k;
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), SR, (unsigned) b.getNumChannels(), 24, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            std::printf ("WAV: %s\n", f.getFullPathName().toRawUTF8());
        }
}

static void snapshot (const juce::File& png)
{
    std::vector<double> truth;
    const auto voice = makeVoice (truth);
    TuneProcessor p;
    p.setPlayConfigDetails (2, 2, SR, 512);
    p.prepareToPlay (SR, 512);
    setP (p, "velocidade", 30);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* te = dynamic_cast<TuneEditor*> (ed.get());
    juce::AudioBuffer<float> buf (2, 512);
    const int stop = (int) (SR * 4.6);
    for (int pos = 0; pos + 512 <= stop; pos += 512)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, voice[(size_t) (pos + i)]);
        juce::MidiBuffer m; p.processBlock (buf, m);
        if ((pos / 512) % 3 == 0) te->tick();
    }
    te->tick();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("PNG: %s\n", png.getFullPathName().toRawUTF8());
}

// procesa un ficheiro: BTRenderTest --file entrada.wav saida.wav [parámetro=valor ...]
static int processFile (int argc, char** argv)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File { juce::String (argv[2]) }));
    if (rd == nullptr) { std::printf ("non podo ler %s\n", argv[2]); return 1; }
    const double sr = rd->sampleRate;
    const int len = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> src ((int) rd->numChannels, len);
    rd->read (&src, 0, len, 0, true, true);
    TuneProcessor p;
    p.setPlayConfigDetails (1, 1, sr, 512);
    for (int a = 4; a < argc; ++a)
    {
        const juce::String kv (argv[a]);
        setP (p, kv.upToFirstOccurrenceOf ("=", false, false).toRawUTF8(), kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    }
    p.prepareToPlay (sr, 512);
    const int lat = p.getLatencySamples(), total = len + lat;
    juce::AudioBuffer<float> out (1, len), buf (1, 512);
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = juce::jmin (512, total - pos);
        buf.setSize (1, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            float x = 0;
            if (pos + i < len) { for (int c = 0; c < src.getNumChannels(); ++c) x += src.getSample (c, pos + i); x /= (float) src.getNumChannels(); }
            buf.setSample (0, i, x);
        }
        juce::MidiBuffer m; p.processBlock (buf, m);
        for (int i = 0; i < n; ++i) if (pos + i - lat >= 0 && pos + i - lat < len) out.setSample (0, pos + i - lat, buf.getSample (0, i));
    }
    const juce::File f { juce::String (argv[3]) };
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 1, 24, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (out, 0, len); }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 3 && juce::String (argv[1]) == "--file") return processFile (argc, argv);
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") { snapshot (juce::File { juce::String (argv[2]) }); return 0; }

    std::vector<double> truth;
    const auto voice = makeVoice (truth);
    int fails = 0, lat = 0;
    auto check = [&] (const char* what, bool ok) { std::printf ("%-46s %s\n", what, ok ? "OK" : "FALLO"); fails += ok ? 0 : 1; };
    auto major = [] (TuneProcessor& p) { setP (p, "escala", 1); setP (p, "tonica", 0); };

    // 1. robot en Do maior
    auto robot = run (voice, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 0); }, lat);
    auto s = evaluate (robot, lat, truth, 0);
    std::printf ("robot:   entrada %.1f c -> saída %.1f c (máx %.1f c), %d tramas, %d fóra de nota | latencia %d mostras (%.1f ms)\n",
                 s.inC, s.outC, s.maxC, s.frames, s.wrong, lat, 1000.0 * lat / SR);
    check ("robot: saída a menos de 6 cents de media", s.outC < 6.0 && s.wrong < s.frames / 50 + 2);

    // 2. natural
    auto nat = run (voice, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 60); setP (p, "humanizar", 30); }, lat);
    s = evaluate (nat, lat, truth, 0);
    std::printf ("natural: entrada %.1f c -> saída %.1f c (máx %.1f c)\n", s.inC, s.outC, s.maxC);
    check ("natural: corrixe (menos da metade da desafinación)", s.outC < 0.5 * s.inC);

    // 3. transpor +5 (formantes por PSOLA)
    auto tr = run (voice, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 0); setP (p, "transpor", 5); }, lat);
    s = evaluate (tr, lat, truth, 5);
    std::printf ("+5 st:   saída %.1f c (máx %.1f c), %d fóra de nota\n", s.outC, s.maxC, s.wrong);
    check ("transpor +5: notas certas", s.outC < 8.0 && s.wrong < s.frames / 50 + 2);

    // 4. MIDI nota: unha terceira por riba da melodía
    {
        std::vector<std::pair<int, int>> ev;
        double t = 0;
        for (auto& nt : melody()) { const int n = (int) nt.midi + 4; ev.push_back ({ (int) (t * SR), n }); ev.push_back ({ (int) ((t + nt.dur) * SR) - 48, -n }); t += nt.dur; }
        auto mid = run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "modo", 2); setP (p, "velocidade", 0); }, lat, ev);
        s = evaluate (mid, lat, truth, 4);
        std::printf ("MIDI:    saída %.1f c (máx %.1f c), %d fóra de nota\n", s.outC, s.maxC, s.wrong);
        check ("MIDI nota: segue o teclado", s.outC < 8.0 && s.wrong < s.frames / 20 + 2);
    }

    // 5. bloques de 37 e mono dan o mesmo
    {
        int l2 = 0, l3 = 0;
        auto b37 = run (voice, 37, 2, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 0); }, l2);
        auto mono = run (voice, 512, 1, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 0); }, l3);
        float d1 = 0, d2 = 0;
        for (int i = 0; i < robot.getNumSamples(); ++i)
        {
            d1 = std::max (d1, std::abs (robot.getSample (0, i) - b37.getSample (0, i)));
            d2 = std::max (d2, std::abs (robot.getSample (1, i) - mono.getSample (0, i)));
        }
        std::printf ("diferenzas: bloques 37 %.2g, mono %.2g\n", d1, d2);
        check ("bloques de 37 = bloques de 512", d1 < 1.0e-6f);
        check ("mono = estéreo", d2 < 1.0e-6f);
    }

    // 6. mestura 0 = voz orixinal atrasada a latencia
    {
        auto dry = run (voice, 256, 2, [&] (TuneProcessor& p) { setP (p, "mestura", 0); }, lat);
        float d = 0;
        for (int i = 0; i < (int) voice.size(); ++i) d = std::max (d, std::abs (dry.getSample (0, i + lat) - voice[(size_t) i]));
        check ("mestura 0 = orixinal aliñada", d < 1.0e-6f);
    }

    // 7. estado
    {
        TuneProcessor a;
        setP (a, "escala", 6); setP (a, "tonica", 9); setP (a, "velocidade", 123); setP (a, "modo", 1); setP (a, "transpor", -3);
        a.toggleScaleNote (1);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        TuneProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool ok = true;
        for (auto* prm : a.getParameters())
            if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
                ok &= std::abs (prm->getValue() - b.params.getParameter (wp->paramID)->getValue()) < 1.0e-6f;
        ok &= a.effectiveMask() == b.effectiveMask() && (int) b.params.getRawParameterValue ("escala")->load() == scales::custom;
        check ("estado gardado e recuperado", ok);
    }

    // 8. silencio e ruído: nada raro
    {
        std::vector<float> noise (48000 * 2);
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (size_t i = 24000; i < noise.size(); ++i) noise[i] = u (rng);
        auto y = run (noise, 512, 2, [] (TuneProcessor&) {}, lat);
        float mx = 0; bool finite = true;
        for (int i = 0; i < y.getNumSamples(); ++i) { const float v = y.getSample (0, i); finite &= std::isfinite (v); mx = std::max (mx, std::abs (v)); }
        std::printf ("ruído: pico %.2f\n", mx);
        check ("silencio e ruído sen valores raros", finite && mx < 2.0f);
    }

    if (argc > 1)
    {
        const juce::File f { juce::String (argv[1]) };
        writeWav (f, robot);
        writeWav (f.getSiblingFile (f.getFileNameWithoutExtension() + "_natural.wav"), nat);
        writeWav (f.getSiblingFile (f.getFileNameWithoutExtension() + "_mais5.wav"), tr);
    }
    std::printf (fails == 0 ? "TODO OK\n" : "%d FALLOS\n", fails);
    return fails;
}
