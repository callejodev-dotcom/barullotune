// Proba sen interface: unha voz sintética desafinada pasa polo procesador e mídese
// a afinación da saída. Tamén: independencia do tamaño de bloque, mono, MIDI,
// estado gardado e (con --snapshot) unha captura da interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include <map>
#include <cstring>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static const double SR = 48000.0;

// métricas (para --json: o CI compáraas coa base e falla se algo empeora) e huella da
// saída (para comprobar que un cambio de organización do código non cambia nin un bit)
static std::map<std::string, double> metrics;
static void metric (const std::string& k, double v) { metrics[k] = v; }
static uint64_t outHash = 1469598103934665603ull;
static void hashBuffer (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            uint32_t u; const float v = b.getSample (c, i); std::memcpy (&u, &v, 4);
            outHash = (outHash ^ u) * 1099511628211ull;
        }
}

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

struct Stats { double inC = 0, outC = 0, maxC = 0, signedC = 0; int frames = 0, wrong = 0; };

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
        s.outC += e; s.maxC = std::max (s.maxC, e); s.signedC += (mo - w) * 100; s.frames++;
    }
    if (s.frames > 0) { s.inC /= (s.frames + s.wrong); s.outC /= s.frames; s.signedC /= s.frames; }
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
    hashBuffer (out);
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

// tempo real por instancia: voz sintética en bucle, estéreo, bloques de 128
static void cpuBench (double seconds)
{
    std::vector<double> tr;
    const auto v = makeVoice (tr);
    for (int q = 0; q < 6; ++q)
    {
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 128);
        setP (p, "calidade", (float) (q == 3 || q == 4 ? 0 : q == 5 ? 2 : q));
        if (q == 3) setP (p, "transpor", 5);   // directo transpondo: coa corrección de formantes
        if (q >= 4) for (int h = 1; h <= 4; ++h) setP (p, ("harm" + juce::String (h)).toRawUTF8(), 1);   // catro armonías
        p.prepareToPlay (SR, 128);
        juce::AudioBuffer<float> buf (2, 128);
        const int blocks = (int) (seconds * SR / 128);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buf.setSample (c, i, v[(size_t) ((b * 128 + i) % (int) v.size())]);
            juce::MidiBuffer m; p.processBlock (buf, m);
        }
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        const char* name = q == 0 ? "directo" : q == 1 ? "preciso" : q == 2 ? "estudio" : q == 3 ? "directo +5 (formantes)" : q == 4 ? "directo + 4 armonías" : "estudio + 4 armonías";
        std::printf ("%s: %.0f veces tempo real (%.2f %% dun núcleo)\n", name, seconds / secs, 100.0 * secs / seconds);
        metric (std::string ("cpu.") + (q == 0 ? "directo" : q == 1 ? "preciso" : q == 2 ? "estudio" : q == 3 ? "directo_formantes" : q == 4 ? "directo_armonias" : "estudio_armonias"), 100.0 * secs / seconds);
    }
}

static void writeJson (const juce::File& f)
{
    juce::String s ("{\n");
    int i = 0;
    for (auto& kv : metrics)
        s << "  \"" << kv.first << "\": " << juce::String (kv.second, 4) << (++i < (int) metrics.size() ? ",\n" : "\n");
    s << "}\n";
    f.replaceWithText (s);
    std::printf ("JSON: %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 3 && juce::String (argv[1]) == "--file") return processFile (argc, argv);
    if (argc > 1 && juce::String (argv[1]) == "--cpu") { cpuBench (20.0); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") { snapshot (juce::File { juce::String (argv[2]) }); return 0; }
    // BTRenderTest [saida.wav] [--json metricas.json]
    juce::String wavOut, jsonOut;
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (arg == "--json" && a + 1 < argc) jsonOut = argv[++a];
        else wavOut = arg;
    }

    std::vector<double> truth;
    const auto voice = makeVoice (truth);
    int fails = 0, lat = 0;
    auto check = [&] (const char* what, bool ok) { std::printf ("%-46s %s\n", what, ok ? "OK" : "FALLO"); fails += ok ? 0 : 1; };
    auto major = [] (TuneProcessor& p) { setP (p, "escala", 1); setP (p, "tonica", 0); };

    juce::AudioBuffer<float> robot, nat, tr;
    for (int qi = 0; qi < 3; ++qi)
    {
        const int q = qi == 0 ? 0 : (qi == 1 ? 2 : 1);   // directo, estudio, preciso
        const char* qn = q == 0 ? "directo" : q == 1 ? "preciso" : "estudio";
        auto setq = [q] (TuneProcessor& p) { setP (p, "calidade", (float) q); };
        std::printf ("---- %s\n", qn);

        // robot en Do maior
        auto rb = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, lat);
        auto s = evaluate (rb, lat, truth, 0);
        std::printf ("robot:   entrada %.1f c -> saída %.1f c (máx %.1f c), %d tramas, %d fóra de nota | latencia %d mostras (%.2f ms)\n",
                     s.inC, s.outC, s.maxC, s.frames, s.wrong, lat, 1000.0 * lat / SR);
        const std::string M (qn);
        metric (M + ".robot.cents", s.outC); metric (M + ".robot.fora", s.wrong); metric (M + ".latencia.ms", 1000.0 * lat / SR);
        check (q == 0 ? "directo robot: menos de 8 cents de media" : q == 1 ? "preciso robot: menos de 3 cents de media" : "estudio robot: menos de 3 cents de media",
               s.outC < (q == 0 ? 8.0 : 3.0) && s.wrong < s.frames / 30 + 2);
        if (q == 0) check ("directo: latencia de menos de 1 ms", lat < SR * 0.001);
        if (q == 1) check ("preciso: latencia de menos de 13 ms", lat < SR * 0.013);

        // natural
        auto nt = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 60); setP (p, "transicion", 120); }, lat);
        s = evaluate (nt, lat, truth, 0);
        std::printf ("natural: entrada %.1f c -> saída %.1f c (máx %.1f c)\n", s.inC, s.outC, s.maxC);
        metric (M + ".natural.cents", s.outC);
        check ("natural: corrixe (menos da metade da desafinación)", s.outC < 0.5 * s.inC);

        // vibrato de verdade: unha nota longa (2,4 s) 25 cents baixa cun vibrato regular de
        // 5,5 Hz e ±30 cents. Con vibrato 100 %: centrada na nota e co vibrato enteiro;
        // con vibrato 0 %: plana.
        {
            std::vector<float> vn ((size_t) (2.4 * SR));
            double ph = 0;
            for (size_t i = 0; i < vn.size(); ++i)
            {
                const double tt = (double) i / SR;
                const double mm = 63.75 + 0.30 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * tt);
                const double f0 = 440.0 * std::pow (2.0, (mm - 69) / 12.0);
                ph += f0 / SR;
                double sm = 0;
                for (int h = 1; h <= 30; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                vn[i] = (float) (0.1 * sm * std::min (1.0, tt / 0.03));
            }
            auto stats = [&] (const juce::AudioBuffer<float>& y, int l, double& centre, double& depth)
            {
                std::vector<double> ms;
                for (int64_t c = (int64_t) (0.6 * SR); c < (int64_t) (2.2 * SR); c += 240)
                {
                    const double mo = measure (y.getReadPointer (0), y.getNumSamples(), c + l);
                    if (mo > 0) ms.push_back (mo);
                }
                double mean = 0; for (double v : ms) mean += v; mean /= (double) std::max<size_t> (1, ms.size());
                double var = 0; for (double v : ms) var += (v - mean) * (v - mean); var /= (double) std::max<size_t> (1, ms.size());
                centre = (mean - 64.0) * 100.0; depth = std::sqrt (var) * 100.0 * std::sqrt (2.0);   // amplitude do seno
            };
            int l1 = 0, l2 = 0;
            auto v100 = run (vn, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 100); }, l1);
            auto v0 = run (vn, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, l2);
            double c1, d1, c0, d0;
            stats (v100, l1, c1, d1); stats (v0, l2, c0, d0);
            std::printf ("vibrato: entrada 25 c baixa e ±30 c | vibrato 100 %%: centro %+.1f c, ±%.1f c | vibrato 0 %%: centro %+.1f c, ±%.1f c\n", c1, d1, c0, d0);
            metric (M + ".vibrato100.centro.cents", std::abs (c1)); metric (M + ".vibrato100.fondo", d1);
            metric (M + ".vibrato0.centro.cents", std::abs (c0)); metric (M + ".vibrato0.fondo.cents", d0);
            check ("vibrato 100 %: enteiro e centrado; 0 %: plano", std::abs (c1) < 4.0 && d1 > 22.0 && d1 < 38.0 && std::abs (c0) < 4.0 && d0 < 6.0);
        }

        // inseguridade (oscilación irregular, non vibrato): o preset Natural debe corrixila
        {
            std::vector<float> wn ((size_t) (2.4 * SR));
            std::mt19937 rr (11); std::normal_distribution<double> gn (0, 1);
            double ph = 0, w1 = 0, w2 = 0;
            std::vector<double> wob (wn.size());
            for (size_t i = 0; i < wn.size(); ++i)
            {
                // ruído filtrado (1–8 Hz aprox.): oscilación sen período fixo
                w1 += (gn (rr) - w1) * (2 * juce::MathConstants<double>::pi * 8.0 / SR) * 3.0;
                w2 += (w1 - w2) * (2 * juce::MathConstants<double>::pi * 2.0 / SR);
                wob[i] = w1 - w2;
            }
            double sd = 0; for (double v : wob) sd += v * v; sd = std::sqrt (sd / (double) wob.size());
            for (size_t i = 0; i < wn.size(); ++i)
            {
                const double tt = (double) i / SR;
                const double mm = 64.2 + 0.25 * wob[i] / sd;
                const double f0 = 440.0 * std::pow (2.0, (mm - 69) / 12.0);
                ph += f0 / SR;
                double sm = 0;
                for (int h = 1; h <= 30; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                wn[i] = (float) (0.1 * sm * std::min (1.0, tt / 0.03));
            }
            auto spread = [&] (const float* y, int len, int l)
            {
                double acc = 0; int cnt = 0;
                for (int64_t c = (int64_t) (0.6 * SR); c < (int64_t) (2.2 * SR); c += 240)
                {
                    const double mo = measure (y, len, c + l);
                    if (mo > 0) { acc += (mo - 64.0) * (mo - 64.0); ++cnt; }
                }
                return std::sqrt (acc / std::max (1, cnt)) * 100.0;
            };
            int lw = 0;
            auto natw = run (wn, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "escala", 0); }, lw);   // Natural (por defecto)
            const double sin_ = spread (wn.data(), (int) wn.size(), 0), sout = spread (natw.getReadPointer (0), natw.getNumSamples(), lw);
            std::printf ("inseguridade: desviación da nota %.1f c -> %.1f c (Natural)\n", sin_, sout);
            metric (M + ".inseguridade.cents", sout);
            check ("Natural corrixe a inseguridade (non a toma por vibrato)", sout < 0.5 * sin_);
        }

        // voz «de verdade» nunha nota: vibrato rápido e ancho (6,2 Hz, ±45 c), tremor de
        // período ciclo a ciclo (0,6 %) e de nivel. En robot (vibrato 0 %) a saída ten que
        // quedar plana na nota: así se ve se o ton se mide ben no punto que se le
        {
            std::vector<float> vr ((size_t) (2.4 * SR));
            std::mt19937 rr (5); std::normal_distribution<double> gn (0, 1);
            double ph = 0, jit = 0, shim = 1.0;
            for (size_t i = 0; i < vr.size(); ++i)
            {
                const double tt = (double) i / SR;
                const double mm = 64.0 + 0.45 * std::sin (2 * juce::MathConstants<double>::pi * 6.2 * tt);
                const double f0 = 440.0 * std::pow (2.0, (mm - 69) / 12.0) * (1.0 + jit);
                const double before = ph;
                ph += f0 / SR;
                if (std::floor (ph) != std::floor (before)) { jit = 0.006 * gn (rr); shim = 1.0 + 0.08 * gn (rr); }   // ciclo novo
                double sm = 0;
                for (int h = 1; h <= 30; ++h) sm += envAt (VE, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                vr[i] = (float) (0.1 * shim * sm * std::min (1.0, tt / 0.03));
            }
            int lr = 0;
            auto ro = run (vr, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, lr);
            double acc = 0, ain = 0; int cnt = 0;
            for (int64_t c = (int64_t) (0.4 * SR); c < (int64_t) (2.2 * SR); c += 240)
            {
                const double mo = measure (ro.getReadPointer (0), ro.getNumSamples(), c + lr);
                const double mi = measure (vr.data(), (int64_t) vr.size(), c);
                if (mo > 0 && mi > 0) { acc += (mo - 64.0) * (mo - 64.0); ain += (mi - 64.0) * (mi - 64.0); ++cnt; }
            }
            const double rin = std::sqrt (ain / std::max (1, cnt)) * 100.0, rout = std::sqrt (acc / std::max (1, cnt)) * 100.0;
            std::printf ("voz con vibrato rápido e tremor, robot: %.1f c -> %.1f c da nota\n", rin, rout);
            metric (M + ".tremor.cents", rout);
            const double lim = q == 0 ? 4.0 : 1.5;   // antes do ton deslizante: directo 6,0 c, preciso 1,8 c
            check (q == 0 ? "directo: vibrato rápido aplanado (menos de 4 c)" : q == 1 ? "preciso: vibrato rápido aplanado (menos de 1,5 c)" : "estudio: vibrato rápido aplanado (menos de 1,5 c)", rout < lim);
        }

        // glisandos (efecto robot, cromática): a saída ten que ir en escaleira detrás da voz,
        // cambiando de nota onde a voz cruza a metade. Antes a nota quedaba ata dous
        // semitonos atrás nos glisandos rápidos (40 st/s)
        {
            struct Seg { double from, to, dur; };
            const Seg segs[] = { { 57, 57, 0.4 }, { 57, 64, 0.18 }, { 64, 64, 0.35 }, { 64, 59, 0.12 }, { 59, 59, 0.35 },
                                 { 59, 66, 0.5 }, { 66, 66, 0.3 }, { 66, 61, 0.25 }, { 61, 61, 0.4 }, { 61, 63.2, 0.06 }, { 63.2, 63.2, 0.35 } };
            std::vector<float> gv; std::vector<double> gt;
            double ph = 0;
            for (auto& sg : segs)
                for (int i = 0; i < (int) (sg.dur * SR); ++i)
                {
                    const double u = (double) i / (sg.dur * SR);
                    const double mm = sg.from + (sg.to - sg.from) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * u));
                    const double f0 = 440.0 * std::pow (2.0, (mm - 69) / 12.0);
                    ph += f0 / SR;
                    double sm = 0;
                    for (int h = 1; h <= 30; ++h) sm += envAt (VO, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                    gv.push_back ((float) (0.1 * sm * std::min (1.0, (double) gv.size() / (0.03 * SR))));
                    gt.push_back (mm);
                }
            int lg = 0;
            auto go = run (gv, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, lg);
            int bad = 0, cnt = 0;
            for (int64_t c = (int64_t) (0.2 * SR); c < (int64_t) gt.size() - 2400; c += 240)
            {
                const double mo = measure (go.getReadPointer (0), go.getNumSamples(), c + lg);
                if (mo < 0) continue;
                ++cnt;
                if (std::abs (mo - gt[(size_t) c]) > 1.1) ++bad;   // escaleira: como moito medio semitono máis a histérese
            }
            const double pct = 100.0 * bad / std::max (1, cnt);
            std::printf ("glisandos: %.1f %% de tramas a máis de 1,1 semitonos da voz (%d tramas)\n", pct, cnt);
            metric (M + ".glisandos.pct", pct);
            // antes da decisión por voz: directo 22,6 %, preciso 15,8 %, estudio 21,8 %
            check (q == 0 ? "directo: glisandos sen arrastrar a nota" : q == 1 ? "preciso: glisandos sen arrastrar a nota" : "estudio: glisandos sen arrastrar a nota", pct < 5.0);
        }

        // tolerancia 40 cents e notas libres: case non se toca
        {
            auto tl = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "tolerancia", 40); }, lat);
            auto st = evaluate (tl, lat, truth, 0);
            auto lb = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); for (int i = 0; i < 12; ++i) setP (p, ("libre" + juce::String (i)).toRawUTF8(), 1); }, lat);
            auto sl = evaluate (lb, lat, truth, 0);
            std::printf ("tolerancia 40 c: saída %.1f c | todas libres: saída %.1f c (entrada %.1f c)\n", st.outC, sl.outC, sl.inC);
            check ("tolerancia e notas libres: deixan a voz", st.outC > 0.6 * st.inC && sl.outC > 0.85 * sl.inC);
        }

        // transpor +5
        auto t5 = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "transpor", 5); }, lat);
        s = evaluate (t5, lat, truth, 5);
        std::printf ("+5 st:   saída %.1f c (máx %.1f c), %d fóra de nota\n", s.outC, s.maxC, s.wrong);
        metric (M + ".transpor5.cents", s.outC); metric (M + ".transpor5.fora", s.wrong);
        check ("transpor +5: notas certas", s.outC < 8.0 && s.wrong < s.frames / 30 + 2);

        // MIDI nota: unha terceira por riba da melodía
        {
            std::vector<std::pair<int, int>> ev;
            double t = 0;
            for (auto& m : melody()) { const int n = (int) m.midi + 4; ev.push_back ({ (int) (t * SR), n }); ev.push_back ({ (int) ((t + m.dur) * SR) - 48, -n }); t += m.dur; }
            auto mid = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "modo", 2); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, lat, ev);
            s = evaluate (mid, lat, truth, 4);
            std::printf ("MIDI:    saída %.1f c (máx %.1f c), %d fóra de nota\n", s.outC, s.maxC, s.wrong);
            metric (M + ".midi.cents", s.outC); metric (M + ".midi.fora", s.wrong);
            check ("MIDI nota: segue o teclado", s.outC < 8.0 && s.wrong < s.frames / 20 + 2);
        }

        // bloques de 37 e mono dan o mesmo
        {
            int l2 = 0, l3 = 0;
            auto b37 = run (voice, 37, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, l2);
            auto mono = run (voice, 512, 1, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, l3);
            float d1 = 0, d2 = 0;
            for (int i = 0; i < rb.getNumSamples(); ++i)
            {
                d1 = std::max (d1, std::abs (rb.getSample (0, i) - b37.getSample (0, i)));
                d2 = std::max (d2, std::abs (rb.getSample (1, i) - mono.getSample (0, i)));
            }
            check ("bloques de 37 = bloques de 512", d1 < 1.0e-6f);
            check ("mono = estéreo", d2 < 1.0e-6f);
        }

        // mestura 0 = voz orixinal atrasada a latencia
        {
            auto dry = run (voice, 256, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "mestura", 0); }, lat);
            float d = 0;
            for (int i = 0; i < (int) voice.size(); ++i) d = std::max (d, std::abs (dry.getSample (0, i + lat) - voice[(size_t) i]));
            check ("mestura 0 = orixinal aliñada", d < 1.0e-6f);
        }

        // sen corrección: a voz pasa case igual (periodicidade e nivel)
        {
            auto c0 = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "cantidade", 0); }, lat);
            double ein = 0, eout = 0;
            for (size_t i = 0; i < voice.size(); ++i) { ein += voice[i] * voice[i]; eout += c0.getSample (0, (int) i + lat) * c0.getSample (0, (int) i + lat); }
            const double db = 10 * std::log10 (eout / ein);
            std::printf ("cantidade 0: nivel %+.2f dB\n", db);
            check ("cantidade 0: mesmo nivel (±1 dB)", std::abs (db) < 1.0);
        }
        if (q == 0) { robot = rb; nat = nt; tr = t5; }
        std::printf ("---- (fin %s)\n", qn);
    }

    // estéreo de verdade: L = voz, R = 0,6 · voz atrasada 3 ms -> as dúas canles corríxense igual
    for (int q = 0; q < 3; q += 2)
    {
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 512);
        setP (p, "calidade", (float) q); setP (p, "escala", 1); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0);
        p.prepareToPlay (SR, 512);
        const int total = (int) voice.size();
        juce::AudioBuffer<float> buf (2, 512), outL (1, total), outR (1, total);
        const int dl = (int) (0.003 * SR);
        for (int pos = 0; pos < total; pos += 512)
        {
            const int nn = juce::jmin (512, total - pos);
            buf.setSize (2, nn, false, false, true);
            for (int i = 0; i < nn; ++i)
            {
                buf.setSample (0, i, voice[(size_t) (pos + i)]);
                buf.setSample (1, i, pos + i >= dl ? 0.6f * voice[(size_t) (pos + i - dl)] : 0.0f);
            }
            juce::MidiBuffer m; p.processBlock (buf, m);
            outL.copyFrom (0, pos, buf, 0, 0, nn); outR.copyFrom (0, pos, buf, 1, 0, nn);
        }
        // a canle dereita debe ser a esquerda corrixida igual: medímolo co mesmo atraso e ganancia
        double el = 0, er = 0;
        for (int i = (int) SR; i < total - dl; ++i) { el += outL.getSample (0, i) * outL.getSample (0, i); er += outR.getSample (0, i + dl) * outR.getSample (0, i + dl); }
        int lat2 = p.getLatencySamples();
        auto sl = evaluate (outL, lat2, truth, 0);
        std::printf ("estéreo %s: esquerda %.1f c, nivel dereita/esquerda %.3f (agardado 0,600)\n", q == 0 ? "directo" : "estudio", sl.outC, std::sqrt (er / el));
        metric (std::string (q == 0 ? "directo" : "estudio") + ".estereo.cents", sl.outC);
        check (q == 0 ? "directo: estéreo corrixido igual nas dúas canles" : "estudio: estéreo corrixido igual nas dúas canles",
               sl.outC < (q == 0 ? 8.0 : 3.0) && std::abs (std::sqrt (er / el) - 0.6) < 0.03);
    }

    // comparar: sae a voz orixinal, aliñada
    {
        int l = 0;
        auto cmp = run (voice, 256, 2, [&] (TuneProcessor& p) { major (p); setP (p, "comparar", 1); }, l);
        float d = 0;
        for (int i = 0; i < (int) voice.size(); ++i) d = std::max (d, std::abs (cmp.getSample (0, i + l) - voice[(size_t) i]));
        check ("comparar = voz orixinal aliñada", d < 1.0e-6f);
    }

    // voz moi baixa (−40 dB): ten que afinala igual (antes o detector tomábaa por silencio)
    {
        std::vector<float> quiet (voice);
        for (auto& v : quiet) v *= 0.01f;
        int l = 0;
        auto y = run (quiet, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, l);
        y.applyGain (100.0f);
        auto sq = evaluate (y, l, truth, 0);
        std::printf ("voz a -40 dB: saída %.1f c, %d tramas\n", sq.outC, sq.frames);
        metric ("directo.baixa40db.cents", sq.outC);
        check ("voz baixa (-40 dB): afínaa igual", sq.frames > 400 && sq.outC < 8.0);
    }

    // formantes (estudio): a nota non cambia e o timbre si
    {
        int l1 = 0, l2 = 0;
        auto base = run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, l1);
        auto up = run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "formantes", 4); }, l2);
        auto s = evaluate (up, l2, truth, 0);
        auto centroid = [] (const juce::AudioBuffer<float>& b)
        {
            const float* x = b.getReadPointer (0); double num = 0, den = 0, prev = 0;
            for (int i = 1; i < b.getNumSamples(); ++i) { const double d = x[i] - x[i - 1]; num += d * d; den += (double) x[i] * x[i]; prev = d; }
            (void) prev;
            return std::sqrt (num / std::max (1e-12, den)) * SR / (2 * juce::MathConstants<double>::pi);
        };
        const double c0 = centroid (base), c1 = centroid (up);
        std::printf ("formantes +4: saída %.1f c, brillo %.0f Hz -> %.0f Hz\n", s.outC, c0, c1);
        metric ("estudio.formantes4.cents", s.outC);
        check ("formantes +4: mesma nota e timbre máis brillante", s.outC < 4.0 && c1 > c0 * 1.12);
    }

    // MIDI nota + pitch bend: a roda arriba do todo sobe a voz 2 semitonos
    {
        std::vector<std::pair<int, int>> ev;
        double t = 0;
        for (auto& m : melody()) { const int nn = (int) m.midi; ev.push_back ({ (int) (t * SR), nn }); ev.push_back ({ (int) ((t + m.dur) * SR) - 48, -nn }); t += m.dur; }
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 512);
        setP (p, "modo", 2); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0);
        p.prepareToPlay (SR, 512);
        const int lb = p.getLatencySamples(), total = (int) voice.size() + lb;
        juce::AudioBuffer<float> out (2, total), buf (2, 512);
        size_t k = 0;
        for (int pos = 0; pos < total; pos += 512)
        {
            const int nn = juce::jmin (512, total - pos);
            buf.setSize (2, nn, false, false, true);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < nn; ++i) buf.setSample (c, i, pos + i < (int) voice.size() ? voice[(size_t) (pos + i)] : 0.0f);
            juce::MidiBuffer midi;
            if (pos == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
            while (k < ev.size() && ev[k].first < pos + nn)
            {
                const int at = juce::jmax (0, ev[k].first - pos), note = ev[k].second;
                midi.addEvent (note > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, -note), at);
                ++k;
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, nn);
        }
        auto sb = evaluate (out, lb, truth, 2);
        std::printf ("MIDI + bend: saída %.1f c, %d fóra de nota\n", sb.outC, sb.wrong);
        metric ("directo.bend.cents", sb.outC);
        check ("pitch bend en MIDI nota: +2 semitonos", sb.outC < 8.0 && sb.wrong < sb.frames / 20 + 2);
    }

    // directo transpondo +5: a nota sobe pero o timbre non (sen voz de esquío).
    // Espectro medio en terzos de oitava (200 Hz–6 kHz), comparado co da voz orixinal.
    {
        auto bands = [] (const float* x, int len)
        {
            std::vector<double> e;
            for (double fc = 200.0; fc <= 6000.0; fc *= std::pow (2.0, 1.0 / 3.0))
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * fc / SR, q = 4.3, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
                const double b0 = al / a0, b2 = -al / a0, a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - al) / a0;
                double z1 = 0, z2 = 0, acc = 0;
                for (int i = 0; i < len; ++i)
                {
                    const double y = b0 * x[i] + z1; z1 = -a1 * y + z2; z2 = b2 * x[i] - a2 * y; acc += y * y;
                }
                e.push_back (10.0 * std::log10 (acc + 1e-20));
            }
            double m = 0; for (double v : e) m += v; m /= (double) e.size();
            for (double& v : e) v -= m;
            return e;
        };
        auto dist = [] (const std::vector<double>& a, const std::vector<double>& b)
        {
            double d = 0; for (size_t i = 0; i < a.size(); ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
            return std::sqrt (d / (double) a.size());
        };
        const double r = std::pow (2.0, 5.0 / 12.0);
        std::vector<float> chip ((size_t) ((double) voice.size() / r));
        for (size_t i = 0; i < chip.size(); ++i)
        {
            const double p = (double) i * r; const size_t i0 = (size_t) p; const float f = (float) (p - (double) i0);
            chip[i] = voice[i0] + (voice[std::min (voice.size() - 1, i0 + 1)] - voice[i0]) * f;
        }
        int l = 0;
        auto d5 = run (voice, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "transpor", 5); }, l);
        auto s5 = run (voice, 512, 2, [&] (TuneProcessor& p) { major (p); setP (p, "transpor", 5); setP (p, "calidade", 2); }, l);
        const auto eo = bands (voice.data(), (int) voice.size());
        const double dc = dist (eo, bands (chip.data(), (int) chip.size()));
        const double dd = dist (eo, bands (d5.getReadPointer (0), d5.getNumSamples()));
        const double ds = dist (eo, bands (s5.getReadPointer (0), s5.getNumSamples()));
        std::printf ("timbre ao transpor +5 (distancia ao orixinal): esquío %.1f dB, directo %.1f dB, estudio %.1f dB\n", dc, dd, ds);
        metric ("directo.timbre5.db", dd); metric ("estudio.timbre5.db", ds);
        check ("directo +5: sen voz de esquío", dd < 0.65 * dc && dd < ds + 1.0);
    }

    // armonías: a terceira diatónica (Do maior) só pola esquerda coa voz principal
    // apagada; en acorde MIDI, cada armonía vai á súa tecla; comparar apágaas
    {
        auto walk = [] (int nt, int deg) { const int mask = 0x0AB5; auto in = [&] (int v) { return (mask >> (((v % 12) + 12) % 12)) & 1; }; while (! in (nt)) --nt; const int dir = deg < 0 ? -1 : 1; for (int k = 0; k < std::abs (deg); ++k) { nt += dir; while (! in (nt)) nt += dir; } return nt; };
        std::vector<int> want3 (voice.size());
        { size_t k = 0; for (auto& nt : melody()) for (int i = 0; i < (int) (nt.dur * SR); ++i) want3[k++] = walk ((int) nt.midi, 2); }
        for (int q = 0; q < 3; ++q)
        {
            int l = 0;
            auto h = run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", (float) q); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0);
                                                                  setP (p, "principal", -48); setP (p, "harm1", 1); setP (p, "harm1_intervalo", 9); setP (p, "harm1_pan", -100); setP (p, "harm1_atraso", 0); setP (p, "harm1_desafinar", 0); }, l);
            double err = 0, eL = 0, eR = 0; int cnt = 0, bad = 0;
            for (int i = 0; i < h.getNumSamples(); ++i) { eL += (double) h.getSample (0, i) * h.getSample (0, i); eR += (double) h.getSample (1, i) * h.getSample (1, i); }
            for (int64_t c = 4800; c + l < (int64_t) h.getNumSamples() - 4800 && c < (int64_t) truth.size(); c += 480)
            {
                if (truth[(size_t) c] < 0) continue;
                bool stable = true;
                for (int64_t k = c - 2400; k <= c + 2400; k += 240) if (k < 0 || k >= (int64_t) truth.size() || truth[(size_t) k] < 0 || std::abs (truth[(size_t) k] - truth[(size_t) c]) > 0.4) stable = false;
                if (! stable) continue;
                const double mo = measure (h.getReadPointer (0), h.getNumSamples(), c + l);
                if (mo < 0) continue;
                const double e = std::abs (mo - want3[(size_t) c]) * 100;
                if (e > 50) ++bad; else { err += e; ++cnt; }
            }
            err /= std::max (1, cnt);
            const double sep = 10 * std::log10 (eL / std::max (1e-20, eR));
            const char* qn = q == 0 ? "directo" : q == 1 ? "preciso" : "estudio";
            std::printf ("armonía 3ª arriba (%s): %.1f c da nota, %d tramas, %d fóra; esquerda/dereita %.0f dB\n", qn, err, cnt, bad, sep);
            metric (std::string (qn) + ".armonia.cents", err);
            check (q == 0 ? "directo: armonía na súa nota e no seu lado" : q == 1 ? "preciso: armonía na súa nota e no seu lado" : "estudio: armonía na súa nota e no seu lado",
                   cnt > 300 && bad < cnt / 30 + 2 && err < (q == 2 ? 3.0 : 8.0) && sep > 40.0);
        }
        // acorde MIDI: teclas 64 e 67 todo o tempo; armonía 1 -> 64, armonía 2 -> 67
        {
            int l = 0;
            std::vector<std::pair<int, int>> ev { { 0, 64 }, { 0, 67 } };
            auto h = run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "principal", -48);
                                                                  setP (p, "armonia_modo", 1); setP (p, "harm1", 1); setP (p, "harm1_pan", -100); setP (p, "harm2", 1); setP (p, "harm2_pan", 100);
                                                                  setP (p, "harm1_desafinar", 0); setP (p, "harm2_desafinar", 0); }, l, ev);
            double e1 = 0, e2 = 0; int c1 = 0, c2 = 0;
            for (int64_t c = 9600; c + l < (int64_t) h.getNumSamples() - 4800 && c < (int64_t) truth.size(); c += 480)
            {
                if (truth[(size_t) c] < 0) continue;
                const double m1 = measure (h.getReadPointer (0), h.getNumSamples(), c + l), m2 = measure (h.getReadPointer (1), h.getNumSamples(), c + l);
                if (m1 > 0) { e1 += std::abs (m1 - 64) * 100; ++c1; }
                if (m2 > 0) { e2 += std::abs (m2 - 67) * 100; ++c2; }
            }
            e1 /= std::max (1, c1); e2 /= std::max (1, c2);
            std::printf ("acorde MIDI (Mi e Sol): armonía 1 a %.1f c de Mi, armonía 2 a %.1f c de Sol\n", e1, e2);
            check ("acorde MIDI: cada armonía na súa tecla", c1 > 300 && c2 > 300 && e1 < 15.0 && e2 < 15.0);
        }
        // comparar: só a voz orixinal (sen armonías)
        {
            int l = 0;
            auto cmp = run (voice, 256, 2, [&] (TuneProcessor& p) { major (p); setP (p, "comparar", 1); setP (p, "harm1", 1); setP (p, "harm2", 1); }, l);
            float d = 0;
            for (int i = 0; i < (int) voice.size(); ++i) d = std::max (d, std::abs (cmp.getSample (0, i + l) - voice[(size_t) i]));
            check ("comparar: sen armonías", d < 1.0e-6f);
        }
    }

    // flexibilidade: unha nota 40 cents alta a propósito queda como está; unha a 10 cents
    // corríxese. Humanizar: nunha nota longa, un vaivén lento (1,5 Hz, ±20 c) respírase;
    // as notas curtas corríxense en seco igual
    {
        auto tone = [&] (const std::vector<double>& mid)
        {
            std::vector<float> v (mid.size()); double ph = 0;
            for (size_t i = 0; i < mid.size(); ++i)
            {
                const double f0 = 440.0 * std::pow (2.0, (mid[i] - 69) / 12.0); ph += f0 / SR; double sm = 0;
                for (int h = 1; h <= 30; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                v[i] = (float) (0.1 * sm * std::min (1.0, (double) i / (0.03 * SR)));
            }
            return v;
        };
        auto centreOf = [&] (const juce::AudioBuffer<float>& y, int l, double t0, double t1, double note, double& spread)
        {
            std::vector<double> ms;
            for (int64_t c = (int64_t) (t0 * SR); c < (int64_t) (t1 * SR); c += 240) { const double mo = measure (y.getReadPointer (0), y.getNumSamples(), c + l); if (mo > 0) ms.push_back (mo); }
            double mean = 0; for (double v : ms) mean += v; mean /= (double) std::max<size_t> (1, ms.size());
            double var = 0; for (double v : ms) var += (v - mean) * (v - mean); spread = std::sqrt (var / (double) std::max<size_t> (1, ms.size())) * 100.0;
            return (mean - note) * 100.0;
        };
        std::vector<double> far ((size_t) (1.2 * SR), 64.40), near ((size_t) (1.2 * SR), 64.10);
        double sp = 0; int l = 0;
        auto robotFlex = [&] (const std::vector<double>& mid, float flex)
        {
            return run (tone (mid), 512, 1, [&] (TuneProcessor& p) { setP (p, "calidade", 1); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "flexibilidade", flex); }, l);
        };
        auto f0 = robotFlex (far, 0); const double cFar0 = centreOf (f0, l, 0.3, 1.1, 64, sp);
        auto f1 = robotFlex (far, 100); const double cFar1 = centreOf (f1, l, 0.3, 1.1, 64, sp);
        auto n1 = robotFlex (near, 100); const double cNear1 = centreOf (n1, l, 0.3, 1.1, 64, sp);
        std::printf ("flexibilidade: nota +40 c -> %+.1f c (sen flexibilidade %+.1f c); nota +10 c con flexibilidade 100 %% -> %+.1f c\n", cFar1, cFar0, cNear1);
        check ("flexibilidade: deixa a nota lonxe e corrixe a de preto", std::abs (cFar0) < 3.0 && cFar1 > 30.0 && std::abs (cNear1) < 3.0);

        // humanizar
        std::vector<double> lng ((size_t) (2.4 * SR));
        for (size_t i = 0; i < lng.size(); ++i) lng[i] = 64.0 + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 1.5 * (double) i / SR);
        auto hum = [&] (float h) { return run (tone (lng), 512, 1, [&] (TuneProcessor& p) { setP (p, "calidade", 1); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "humanizar", h); }, l); };
        double s0 = 0, s1 = 0, sShort = 0;
        auto h0 = hum (0); centreOf (h0, l, 0.8, 2.3, 64, s0);
        auto h1 = hum (100); centreOf (h1, l, 0.8, 2.3, 64, s1);
        centreOf (h1, l, 0.03, 0.13, 64, sShort);   // o comezo da nota (curta): en seco
        std::printf ("humanizar: vaivén de ±20 c nunha nota longa -> %.1f c (sen humanizar %.1f c); primeiros 100 ms %.1f c\n", s1, s0, sShort);
        check ("humanizar: as notas longas respiran e as curtas non", s0 < 3.0 && s1 > 8.0 && sShort < 3.0);
        metric ("flexibilidade.lonxe.cents", std::abs (cFar0)); metric ("flexibilidade.preto.cents", std::abs (cNear1));
    }

    // vibrato xerado: unha nota plana con robot e vibrato propio a 0 sae cun vibrato de
    // ±30 c a 5,5 Hz despois do retardo (300 ms); antes, plana. Trémolo: o nivel oscila
    for (int q : { 0, 2 })
    {
        std::vector<float> flat ((size_t) (2.0 * SR)); double ph = 0;
        for (size_t i = 0; i < flat.size(); ++i)
        {
            const double f0 = 440.0 * std::pow (2.0, (62.0 - 69) / 12.0); ph += f0 / SR; double sm = 0;
            for (int h = 1; h <= 30; ++h) sm += envAt (VE, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
            flat[i] = (float) (0.1 * sm * std::min (1.0, (double) i / (0.03 * SR)));
        }
        int l = 0;
        auto y = run (flat, 512, 1, [&] (TuneProcessor& p) { setP (p, "calidade", (float) q); setP (p, "escala", 0); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0);
                                                             setP (p, "vibgen_fondo", 30); setP (p, "vibgen_velocidade", 5.5f); setP (p, "vibgen_retardo", 300); setP (p, "vibgen_variacion", 0); setP (p, "vibgen_tremolo", 40); }, l);
        std::vector<double> trace; std::vector<double> lv;
        for (int64_t c = (int64_t) (0.05 * SR); c < (int64_t) (1.9 * SR); c += 96)
        {
            trace.push_back (measure (y.getReadPointer (0), y.getNumSamples(), c + l));
            double e = 0; for (int k = -240; k < 240; ++k) e += (double) y.getSample (0, (int) (c + l + k)) * y.getSample (0, (int) (c + l + k));
            lv.push_back (std::sqrt (e / 480.0));
        }
        auto stat = [&] (double t0, double t1, double& depth, double& rate)
        {
            const size_t a = (size_t) ((t0 - 0.05) * SR / 96), b = std::min (trace.size(), (size_t) ((t1 - 0.05) * SR / 96));
            double mn = 1e9, mx = -1e9, mean = 0; int cnt = 0, zc = 0; double prev = 0;
            for (size_t i = a; i < b; ++i) if (trace[i] > 0) { mean += trace[i]; ++cnt; }
            mean /= std::max (1, cnt);
            for (size_t i = a; i < b; ++i) if (trace[i] > 0) { mn = std::min (mn, trace[i]); mx = std::max (mx, trace[i]); const double d = trace[i] - mean; if (i > a && d * prev < 0) ++zc; prev = d; }
            depth = (mx - mn) * 50.0; rate = zc / 2.0 / (t1 - t0);
        };
        double d0, r0, d1, r1;
        stat (0.05, 0.28, d0, r0); stat (0.7, 1.85, d1, r1);
        double lmin = 1e9, lmax = 0;
        for (size_t i = (size_t) ((0.7 - 0.05) * SR / 96); i < lv.size(); ++i) { lmin = std::min (lmin, lv[i]); lmax = std::max (lmax, lv[i]); }
        const double tremDb = 20 * std::log10 (lmax / std::max (1e-9, lmin));
        std::printf ("vibrato xerado (%s): antes do retardo ±%.1f c; despois ±%.1f c a %.1f Hz; trémolo %.1f dB pico a pico\n", q == 0 ? "directo" : "estudio", d0, d1, r1, tremDb);
        check (q == 0 ? "directo: vibrato xerado (fondo, velocidade, retardo e trémolo)" : "estudio: vibrato xerado (fondo, velocidade, retardo e trémolo)",
               d0 < 6.0 && d1 > 24.0 && d1 < 38.0 && r1 > 4.8 && r1 < 6.2 && tremDb > 2.5 && tremDb < 5.0);   // trémolo 40 %: 1 ± 0,2 = 3,5 dB
    }

    // aire: máis enerxía por riba de 4 kHz canto máis aire, e nada no silencio
    {
        auto hfRatio = [&] (float air, double& silence)
        {
            int l = 0;
            std::vector<float> vs (voice); vs.resize (voice.size() + (size_t) (0.5 * SR), 0.0f);   // medio segundo de silencio ao final
            auto y = run (vs, 512, 1, [&] (TuneProcessor& p) { major (p); setP (p, "aire", air); }, l);
            // enerxía por riba de 4 kHz (diferenzas de 2.ª orde) fronte á total, onde hai voz
            double hf = 0, tot = 0; silence = 0;
            const float* x = y.getReadPointer (0);
            for (size_t i = 2; i < truth.size(); ++i)
            {
                const double v = x[i + (size_t) l], d2 = x[i + (size_t) l] - 2.0 * x[i + (size_t) l - 1] + x[i + (size_t) l - 2];
                if (truth[i] >= 0) { hf += d2 * d2; tot += v * v; }
            }
            for (int i = (int) voice.size() + l + (int) (0.1 * SR); i < y.getNumSamples(); ++i) silence = std::max (silence, (double) std::abs (x[i]));
            return 10.0 * std::log10 (hf / std::max (1e-20, tot));
        };
        double s0 = 0, s1 = 0;
        const double r0 = hfRatio (0, s0), r1 = hfRatio (60, s1);
        std::printf ("aire 60 %%: agudos (relación) %+.1f dB fronte a sen aire; no silencio do final, pico %.5f\n", r1 - r0, s1);
        check ("aire: alento con voz, nada sen ela", r1 - r0 > 3.0 && s1 < 1.0e-3);
    }

    // consoantes xordas ao transpor ±7: a «s» sae co seu nivel e sen estalos (antes, en
    // Directo baixando, ata 11 dB máis forte e cun estalo de 10 ms na fronteira coa vogal)
    {
        std::vector<float> fx;
        std::mt19937 rr (21); std::normal_distribution<double> gn (0, 1);
        double ph = 0, hp = 0, pv = 0, z1 = 0, z2 = 0;
        auto vowel = [&] (double dur)
        {
            for (int i = 0; i < (int) (dur * SR); ++i)
            {
                const double f0 = 220.0; ph += f0 / SR; double sm = 0;
                for (int h = 1; h <= 40; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                fx.push_back ((float) (0.1 * sm * std::min (1.0, i / 480.0)));
            }
        };
        vowel (0.4);
        const size_t fa = fx.size();
        for (int i = 0; i < (int) (0.18 * SR); ++i)
        {
            const double w = gn (rr); hp = 0.85 * (hp + w - pv); pv = w;
            const double yv = hp - 1.2 * z1 - 0.5 * z2; z2 = z1; z1 = yv;
            fx.push_back ((float) (0.03 * yv * std::sin (juce::MathConstants<double>::pi * i / (0.18 * SR))));
        }
        const size_t fb = fx.size();
        vowel (0.4);
        auto level = [] (const float* x, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i]; return 10.0 * std::log10 (e / (double) (b - a) + 1e-20); };
        const double li = level (fx.data(), fa + 1000, fb - 1000);
        double worst = 0, worstBurst = -99;
        for (int q = 0; q < 3; ++q)
            for (int semis : { 7, -7 })
            {
                int l = 0;
                auto y = run (fx, 512, 1, [&] (TuneProcessor& p) { setP (p, "calidade", (float) q); setP (p, "escala", 0); setP (p, "transpor", (float) semis); }, l);
                const double lo = level (y.getReadPointer (0), fa + 1000 + (size_t) l, fb - 1000 + (size_t) l);
                worst = std::max (worst, std::abs (lo - li));
                for (size_t w = fa + 960; w + 240 + 960 < fb; w += 240)   // (nos bordos a entrada está case en silencio)
                    worstBurst = std::max (worstBurst, level (y.getReadPointer (0), w + (size_t) l, w + 240 + (size_t) l) - level (fx.data(), w, w + 240));
            }
        std::printf ("«s» ao transpor ±7: nivel como moito %.1f dB distinto, pico de 5 ms como moito %+.1f dB\n", worst, worstBurst);
        metric ("sibilante.nivel.db", worst); metric ("sibilante.pico.db", std::max (0.0, worstBurst));
        check ("transpor: a «s» co seu nivel e sen estalos", worst < 4.0 && worstBurst < 9.0);
    }

    // rango: menos latencia en estudio con voz aguda
    {
        int lg = 0, la = 0;
        run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); setP (p, "rango", 0); }, lg);
        run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); setP (p, "rango", 5); }, la);
        std::printf ("estudio: latencia baixo %.1f ms, soprano %.1f ms\n", 1000.0 * lg / SR, 1000.0 * la / SR);
        metric ("estudio.latencia.baixo.ms", 1000.0 * lg / SR); metric ("estudio.latencia.soprano.ms", 1000.0 * la / SR);
        check ("rango agudo: menos latencia", la < lg / 2);
    }

    // cambiar de xeito no medio: sen valores raros e a latencia nova chega ao DAW
    {
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 512);
        p.prepareToPlay (SR, 512);
        juce::AudioBuffer<float> buf (2, 512);
        bool finite = true;
        for (int blk = 0; blk < 200; ++blk)
        {
            if (blk == 60) setP (p, "calidade", 2);
            if (blk == 120) setP (p, "rango", 2);
            if (blk == 160) setP (p, "calidade", 0);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, voice[(size_t) ((blk * 512 + i) % (int) voice.size())]);
            juce::MidiBuffer m; p.processBlock (buf, m);
            for (int i = 0; i < 512; ++i) finite &= std::isfinite (buf.getSample (0, i));
            if (blk == 100) { p.flushLatencyUpdate(); std::printf ("latencia en estudio: %d mostras\n", p.getLatencySamples()); finite &= p.getLatencySamples() > 1000; }
        }
        p.flushLatencyUpdate();
        check ("cambio de xeito en marcha", finite && p.getLatencySamples() < 48);
    }

    // cambio de xeito sen estalo: a saída baixa en 5 ms, cámbiase e volve subir; a análise
    // segue, así a voz volve enseguida (antes o motor empezaba de cero e, en Estudio, o
    // son volvía de golpe aos 36 ms)
    {
        TuneProcessor p;
        p.setPlayConfigDetails (1, 1, SR, 256);
        p.prepareToPlay (SR, 256);
        juce::AudioBuffer<float> buf (1, 256);
        std::vector<float> out;
        const int sw[] = { 100, 200, 300, 400 };
        for (int blk = 0; blk < 500; ++blk)
        {
            if (blk == sw[0]) setP (p, "calidade", 2);
            if (blk == sw[1]) setP (p, "calidade", 1);
            if (blk == sw[2]) setP (p, "rango", 2);
            if (blk == sw[3]) setP (p, "calidade", 0);
            for (int i = 0; i < 256; ++i) buf.setSample (0, i, voice[(size_t) (blk * 256 + i)]);
            juce::MidiBuffer m; p.processBlock (buf, m);
            for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
        }
        // o salto máis grande dunha mostra á seguinte, preto dos cambios e no resto
        auto maxStep = [&] (size_t a, size_t b) { float mx = 0; for (size_t i = std::max<size_t> (a, 1); i < b && i < out.size(); ++i) mx = std::max (mx, std::abs (out[i] - out[i - 1])); return mx; };
        float nearSw = 0;
        for (int b : sw) nearSw = std::max (nearSw, maxStep ((size_t) b * 256, (size_t) b * 256 + (size_t) (0.06 * SR)));
        const float steady = maxStep ((size_t) (0.5 * SR), (size_t) sw[0] * 256);
        // e canto tarda en volver o son tras pasar a Estudio (nivel en 10 ms)
        int back = -1;
        for (size_t w = (size_t) sw[0] * 256; w + 480 < (size_t) sw[1] * 256 && back < 0; w += 48)
        {
            double e = 0; for (size_t i = w; i < w + 480; ++i) e += (double) out[i] * out[i];
            if (std::sqrt (e / 480.0) > 0.02) back = (int) (w - (size_t) sw[0] * 256);
        }
        std::printf ("cambio de xeito: salto máximo %.3f preto dos cambios (no resto %.3f); o son volve en %.1f ms\n", nearSw, steady, back / 48.0);
        metric ("cambio.salto", nearSw / std::max (1e-6f, steady));
        // antes: salto 3,6 veces o normal e o son volvía aos 33 ms
        check ("cambio de xeito sen estalo e o son volve axiña", nearSw <= steady * 1.5f && back >= 0 && back < (int) (0.025 * SR));
    }

    // detectar a tonalidade: a melodía (Do maior) dúas veces
    {
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 512);
        setP (p, "tonica", 5); setP (p, "escala", 2);   // empeza noutra cousa
        p.prepareToPlay (SR, 512);
        p.startKeyListening();
        juce::AudioBuffer<float> buf (2, 512);
        const int total = (int) voice.size() * 2;
        bool done = false;
        for (int pos = 0; pos + 512 <= total && ! done; pos += 512)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, voice[(size_t) ((pos + i) % (int) voice.size())]);
            juce::MidiBuffer m; p.processBlock (buf, m);
            done = p.finishKeyIfReady();
        }
        const uint16_t want = scales::rotate (scales::all()[1].mask, 0);
        std::printf ("tonalidade: %s %s\n", scales::noteName ((int) p.params.getRawParameterValue ("tonica")->load()).toRawUTF8(),
                     scales::all()[(size_t) p.params.getRawParameterValue ("escala")->load()].name);
        check ("detectar tonalidade: Do maior (ou a súa relativa)", done && p.effectiveMask() == want);
    }

    // tonalidade co acompañamento na entrada lateral: base en Re maior (Re–Sol–La–Re) e a
    // voz cantando só un La (que soa en moitas tonalidades): sae Re maior (ou Si menor)
    {
        TuneProcessor p;
        auto lay = p.getBusesLayout();
        lay.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        const bool layoutOk = p.setBusesLayout (lay);
        p.setRateAndBufferSizeDetails (SR, 512);
        setP (p, "tonica", 5); setP (p, "escala", 2);
        p.prepareToPlay (SR, 512);
        p.startKeyListening();
        juce::AudioBuffer<float> buf (4, 512);
        const int chords[4][3] = { { 50, 54, 57 }, { 55, 59, 62 }, { 57, 61, 64 }, { 50, 54, 57 } };
        double phs[4] = { 0, 0, 0, 0 }, phv = 0;
        bool done = false; int samples = 0;
        for (int pos = 0; pos < (int) (12 * SR) && ! done; pos += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (pos + i) / SR;
                const auto& ch = chords[(int) (t / 1.0) % 4];
                double acc = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const int nt = k < 3 ? ch[k] : ch[0] - 12;   // o baixo
                    phs[k] += 440.0 * std::pow (2.0, (nt - 69) / 12.0) / SR;
                    for (int h = 1; h <= 6; ++h) acc += std::sin (2 * juce::MathConstants<double>::pi * h * phs[k]) / (h * h);
                }
                phv += 440.0 / SR;
                double v = 0; for (int h = 1; h <= 20; ++h) v += envAt (VA, h * 440.0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * phv);
                buf.setSample (0, i, (float) (0.08 * v)); buf.setSample (1, i, (float) (0.08 * v));
                buf.setSample (2, i, (float) (0.05 * acc)); buf.setSample (3, i, (float) (0.05 * acc));
            }
            juce::MidiBuffer m; p.processBlock (buf, m);
            samples += 512;
            done = p.finishKeyIfReady();
        }
        const uint16_t dMajor = scales::rotate (scales::all()[1].mask, 2);
        std::printf ("tonalidade co acompañamento: %s %s (en %.1f s)\n", scales::noteName ((int) p.params.getRawParameterValue ("tonica")->load()).toRawUTF8(),
                     scales::all()[(size_t) p.params.getRawParameterValue ("escala")->load()].name, samples / SR);
        check ("tonalidade co acompañamento na entrada lateral: Re maior", layoutOk && done && p.effectiveMask() == dMajor);
    }

    // presets: Robot pon velocidade, transición e vibrato a 0; o preset gárdase co estado
    {
        TuneProcessor a;
        int robotIdx = -1;
        for (int i = 0; i < a.getNumPrograms(); ++i) if (a.getProgramName (i) == "Robot") robotIdx = i;
        a.setCurrentProgram (robotIdx);
        const bool ok1 = robotIdx >= 0 && a.params.getRawParameterValue ("velocidade")->load() < 0.5f
                      && a.params.getRawParameterValue ("transicion")->load() < 0.5f && a.params.getRawParameterValue ("vibrato")->load() < 0.5f;
        juce::MemoryBlock mb; a.getStateInformation (mb);
        TuneProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("presets: %d, Robot = %d\n", a.getNumPrograms(), robotIdx);
        check ("presets: Robot aplícase e gárdase", ok1 && b.getCurrentProgram() == robotIdx);
    }

    // estado
    {
        TuneProcessor a;
        setP (a, "escala", 6); setP (a, "tonica", 9); setP (a, "velocidade", 123); setP (a, "modo", 1); setP (a, "transpor", -3);
        setP (a, "calidade", 1); setP (a, "rango", 2); setP (a, "formantes", -2.5f);
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

    // silencio e ruído: nada raro (nos dous xeitos)
    for (int q = 0; q < 3; ++q)
    {
        std::vector<float> noise (48000 * 2);
        std::mt19937 rng (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        for (size_t i = 24000; i < noise.size(); ++i) noise[i] = u (rng);
        auto y = run (noise, 512, 2, [q] (TuneProcessor& p) { setP (p, "calidade", (float) q); setP (p, "transpor", 7); }, lat);
        float mx = 0; bool finite = true;
        for (int i = 0; i < y.getNumSamples(); ++i) { const float v = y.getSample (0, i); finite &= std::isfinite (v); mx = std::max (mx, std::abs (v)); }
        check (q == 0 ? "directo: silencio e ruído sen valores raros" : q == 1 ? "preciso: silencio e ruído sen valores raros" : "estudio: silencio e ruído sen valores raros", finite && mx < 2.0f);
    }

    // bypass: a voz orixinal, atrasada exactamente a latencia (o DAW compénsaa igual ca co efecto)
    for (int q = 0; q < 3; ++q)
    {
        int l = 0;
        auto byp = run (voice, 256, 2, [&] (TuneProcessor& p) { setP (p, "calidade", (float) q); major (p); setP (p, "bypass", 1); }, l);
        float d = 0;
        for (int i = 0; i < (int) voice.size(); ++i) d = std::max (d, std::abs (byp.getSample (1, i + l) - voice[(size_t) i]));
        // e o bypass do DAW (processBlockBypassed) fai o mesmo
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 256);
        setP (p, "calidade", (float) q);
        p.prepareToPlay (SR, 256);
        const int lh = p.getLatencySamples(), total = (int) voice.size() + lh;
        juce::AudioBuffer<float> buf (2, 256);
        float dh = 0;
        for (int pos = 0; pos < total; pos += 256)
        {
            const int nn = juce::jmin (256, total - pos);
            buf.setSize (2, nn, false, false, true);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < nn; ++i) buf.setSample (c, i, pos + i < (int) voice.size() ? voice[(size_t) (pos + i)] : 0.0f);
            juce::MidiBuffer m;
            p.processBlockBypassed (buf, m);
            for (int i = 0; i < nn; ++i)
                if (pos + i - lh >= (int) (0.03 * SR) && pos + i - lh < (int) voice.size())   // despois do fundido de entrada (20 ms)
                    dh = std::max (dh, std::abs (buf.getSample (0, i) - voice[(size_t) (pos + i - lh)]));
        }
        check (q == 0 ? "directo: bypass = orixinal aliñada" : q == 1 ? "preciso: bypass = orixinal aliñada" : "estudio: bypass = orixinal aliñada", d < 1.0e-6f && dh < 1.0e-6f);
    }

    // estado de Barullo Tune 0.1 (sen versión, sen bypass, preset só por número): o bypass
    // volve ao valor de fábrica aínda que a instancia o tivese aceso, e o resto chega igual
    {
        TuneProcessor a;
        setP (a, "velocidade", 123); setP (a, "tonica", 9);
        a.setCurrentProgram (4);
        setP (a, "velocidade", 123);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->removeAttribute ("version"); xml->removeAttribute ("presetName"); xml->removeAttribute ("plugin");
        if (auto* e = xml->getChildByAttribute ("id", "bypass")) xml->removeChildElement (e, true);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        TuneProcessor b;
        setP (b, "bypass", 1);
        b.setStateInformation (old.getData(), (int) old.getSize());
        const bool ok1 = b.params.getRawParameterValue ("bypass")->load() < 0.5f
                      && std::abs (b.params.getRawParameterValue ("velocidade")->load() - 123.0f) < 0.01f
                      && (int) b.params.getRawParameterValue ("tonica")->load() == 9 && b.getCurrentProgram() == 4;
        // o preset búscase polo nome
        xml->setAttribute ("preset", 0); xml->setAttribute ("presetName", "Robot");
        juce::MemoryBlock named; juce::AudioProcessor::copyXmlToBinary (*xml, named);
        TuneProcessor c; c.setStateInformation (named.getData(), (int) named.getSize());
        check ("estado de 0.1: migra (bypass de fábrica, preset polo nome)", ok1 && c.getProgramName (c.getCurrentProgram()) == "Robot");
        // estados rotos ou truncados: non pasa nada
        TuneProcessor d;
        std::mt19937 rr (9);
        for (int k = 0; k < 200; ++k)
        {
            juce::MemoryBlock junk (mb);
            const int cut = (int) (rr() % (unsigned) mb.getSize());
            junk.setSize ((size_t) cut);
            if (k % 2) for (int j = 0; j < 8 && cut > 0; ++j) static_cast<char*> (junk.getData())[rr() % (unsigned) cut] = (char) rr();
            d.setStateInformation (junk.getData(), (int) junk.getSize());
        }
        check ("estados rotos: sen problemas", true);
    }

    // MIDI de saída: a melodía (Do maior) dá as súas 12 notas, na orde
    {
        TuneProcessor p;
        p.setPlayConfigDetails (2, 2, SR, 512);
        setP (p, "midi_saida", 1); major (p);
        p.prepareToPlay (SR, 512);
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<int> ons; int offs = 0, lastOn = -1; bool overlap = false;
        for (int pos = 0; pos + 512 <= (int) voice.size(); pos += 512)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, voice[(size_t) (pos + i)]);
            juce::MidiBuffer m; p.processBlock (buf, m);
            for (const auto md : m)
            {
                const auto msg = md.getMessage();
                if (msg.isNoteOn()) { if (lastOn >= 0) overlap = true; ons.push_back (msg.getNoteNumber()); lastOn = msg.getNoteNumber(); }
                else if (msg.isNoteOff()) { ++offs; if (msg.getNoteNumber() == lastOn) lastOn = -1; }
            }
        }
        std::vector<int> want; for (auto& nt : melody()) want.push_back ((int) nt.midi);
        // as notas distintas seguidas (un glisando pode dar unha nota de paso curta)
        size_t k = 0; int extra = 0;
        for (int n : ons) { if (k < want.size() && n == want[k]) ++k; else ++extra; }
        juce::String seq; for (int n : ons) seq << n << " ";
        std::printf ("MIDI de saída: %d notas (%s), %d da melodía na orde, %d de máis, %d apagadas\n", (int) ons.size(), seq.toRawUTF8(), (int) k, extra, offs);
        check ("MIDI de saída: as notas cantadas, unha tras outra", k == want.size() && extra == 0 && ! overlap);
    }

    // MPE: a nota na canle 3 dóbraa o bend da canle 3 (rango 4 st), non o da 5
    {
        auto bendRun = [&] (int bendChannel)
        {
            TuneProcessor p;
            p.setPlayConfigDetails (1, 1, SR, 512);
            setP (p, "modo", 2); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); setP (p, "bend_rango", 4); setP (p, "calidade", 1);
            p.prepareToPlay (SR, 512);
            std::vector<float> e4 ((size_t) (1.0 * SR)); double ph = 0;
            for (size_t i = 0; i < e4.size(); ++i)
            {
                const double f0 = 440.0 * std::pow (2.0, (60.2 - 69) / 12.0); ph += f0 / SR; double sm = 0;
                for (int h = 1; h <= 30; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
                e4[i] = (float) (0.1 * sm * std::min (1.0, (double) i / (0.03 * SR)));
            }
            const int lt = p.getLatencySamples();
            juce::AudioBuffer<float> buf (1, 512), out (1, (int) e4.size() + lt);
            for (int pos = 0; pos < (int) e4.size(); pos += 512)
            {
                const int nn = juce::jmin (512, (int) e4.size() - pos);
                buf.setSize (1, nn, false, false, true);
                for (int i = 0; i < nn; ++i) buf.setSample (0, i, e4[(size_t) (pos + i)]);
                juce::MidiBuffer m;
                if (pos == 0) { m.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::pitchWheel (bendChannel, 16383), 0); }
                p.processBlock (buf, m);
                out.copyFrom (0, pos, buf, 0, 0, nn);
            }
            double acc = 0; int cnt = 0;
            for (int64_t c = (int64_t) (0.3 * SR); c < (int64_t) (0.9 * SR); c += 240) { const double mo = measure (out.getReadPointer (0), out.getNumSamples(), c + lt); if (mo > 0) { acc += mo; ++cnt; } }
            return acc / std::max (1, cnt);
        };
        const double own = bendRun (3), other = bendRun (5);
        std::printf ("MPE: nota Do na canle 3, bend arriba na canle 3 -> %.2f, na canle 5 -> %.2f\n", own, other);
        check ("MPE: o bend da canle da nota, co rango escollido", std::abs (own - 64.0) < 0.1 && std::abs (other - 60.0) < 0.1);
    }

    // escalas e afinación
    {
        // estado da versión 1 con «Personalizada» (daquela o 9): segue sendo Personalizada
        TuneProcessor a;
        a.toggleScaleNote (1);   // pasa a Personalizada
        const uint16_t maskA = a.effectiveMask();
        juce::MemoryBlock mb; a.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->setAttribute ("version", 1);
        if (auto* e = xml->getChildByAttribute ("id", "escala")) e->setAttribute ("value", (double) scales::customV1);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        TuneProcessor b; b.setStateInformation (old.getData(), (int) old.getSize());
        check ("estado da versión 1: Personalizada segue sendo Personalizada", (int) b.params.getRawParameterValue ("escala")->load() == scales::custom && b.effectiveMask() == maskA);

        // frixia dominante en Mi: Mi Fa Sol♯ La Si Do Re
        TuneProcessor f; setP (f, "tonica", 4); setP (f, "escala", 13);
        check ("frixia dominante en Mi", juce::String (scales::all()[13].name) == juce::String ("Frixia dominante")
                                          && f.effectiveMask() == (uint16_t) ((1 << 4) | (1 << 5) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 0) | (1 << 2)));

        // afinación: un Mi en Do maior, robot. Igual: 0 c; xusta: −13,7 c; personalizada +30 c
        std::vector<float> e4 ((size_t) (1.2 * SR)); double ph = 0;
        for (size_t i = 0; i < e4.size(); ++i)
        {
            const double f0 = 440.0 * std::pow (2.0, (64.08 - 69) / 12.0); ph += f0 / SR; double sm = 0;
            for (int h = 1; h <= 30; ++h) sm += envAt (VA, h * f0) / h * std::sin (2 * juce::MathConstants<double>::pi * h * ph);
            e4[i] = (float) (0.1 * sm * std::min (1.0, (double) i / (0.03 * SR)));
        }
        auto centre = [&] (int afin, float cents4)
        {
            int l = 0;
            auto y = run (e4, 512, 1, [&] (TuneProcessor& p) { setP (p, "calidade", 2); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0);
                                                               setP (p, "afinacion", (float) afin); setP (p, "cents4", cents4); }, l);
            double acc = 0; int cnt = 0;
            for (int64_t c = (int64_t) (0.3 * SR); c < (int64_t) (1.1 * SR); c += 240) { const double mo = measure (y.getReadPointer (0), y.getNumSamples(), c + l); if (mo > 0) { acc += mo; ++cnt; } }
            return (acc / std::max (1, cnt) - 64.0) * 100.0;
        };
        const double cEq = centre (0, 0), cJust = centre (1, 0), cCust = centre (2, 30);
        std::printf ("afinación dun Mi en Do maior: igual %+.1f c, xusta %+.1f c (agardado −13,7), personalizada %+.1f c (agardado +30)\n", cEq, cJust, cCust);
        check ("afinación igual, xusta e personalizada", std::abs (cEq) < 1.5 && std::abs (cJust + 13.69) < 1.5 && std::abs (cCust - 30.0) < 1.5);

        // Scala: unha escala xusta de 7 notas (Ptolomeo) en Re, e unha de 17 notas (erro)
        TuneProcessor sc; setP (sc, "tonica", 2);
        juce::String err;
        const bool ok = sc.loadScala ("! ptolemy.scl\n!\nEscala diatónica intensa de Ptolomeo\n 7\n!\n 9/8\n 5/4\n 4/3\n 3/2\n 5/3\n 15/8\n 2/1\n", err);
        double cs[12]; sc.tuningCents (cs);
        const uint16_t want = (uint16_t) ((1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11) | (1 << 1));   // Re maior
        const bool ok1 = ok && sc.effectiveMask() == want && std::abs (cs[6] + 13.69) < 0.05 && std::abs (cs[4] - 3.91) < 0.05 && std::abs (cs[2]) < 0.01;
        juce::String text17 ("17-EDO\n17\n");
        for (int i = 1; i <= 17; ++i) text17 << juce::String (1200.0 * i / 17.0, 3) << "\n";
        juce::String err17;
        const bool bad = ! sc.loadScala (text17, err17);
        std::printf ("Scala: Ptolomeo en Re %s (Fa♯ %+.2f c, Mi %+.2f c); 17-EDO: %s\n", ok1 ? "ben" : "MAL", cs[6], cs[4], err17.toRawUTF8());
        check ("Scala: escala de 7 notas cargada e a de 17 rexeitada", ok1 && bad);
    }

    // automatización ao chou: todos os parámetros, bloques de 1 a 1024 mostras, MIDI, cambios
    // de xeito e de frecuencia de mostraxe, con voz, ruído, silencio, continua, cadrada a 0 dBFS
    // e impulsos. Nada pode saír non finito nin disparado.
    {
        std::mt19937 rr (1234);
        std::uniform_real_distribution<float> u01 (0.0f, 1.0f), u11 (-1.0f, 1.0f);
        TuneProcessor p;
        double sr = SR;
        p.setPlayConfigDetails (2, 2, sr, 1024);
        p.prepareToPlay (sr, 1024);
        juce::AudioBuffer<float> buf (2, 1024);
        bool finite = true; float peak = 0; int blocks = 0;
        const int64_t total = (int64_t) (SR * 60.0);
        int64_t pos = 0; double ph = 0;
        while (pos < total)
        {
            const int nn = (rr() % 8 == 0) ? 1 + (int) (rr() % 8) : 1 + (int) (rr() % 1024);
            if (rr() % 12 == 0)
                for (auto* prm : p.getParameters()) if (rr() % 4 == 0) prm->setValueNotifyingHost (u01 (rr));
            if (rr() % 400 == 0)
            {
                const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 22050.0 };
                sr = rates[rr() % 6];
                p.setPlayConfigDetails (2, 2, sr, 1024);
                p.prepareToPlay (sr, 1024);
            }
            buf.setSize (2, nn, false, false, true);
            const int kind = (int) ((pos / (int64_t) (SR * 1.5)) % 7);
            for (int i = 0; i < nn; ++i)
            {
                float v = 0;
                const int64_t t = pos + i;
                switch (kind)
                {
                    case 0: v = voice[(size_t) (t % (int64_t) voice.size())]; break;
                    case 1: v = 0.5f * u11 (rr); break;
                    case 2: v = 0; break;
                    case 3: v = 0.7f; break;
                    case 4: ph += 180.0 / sr; v = std::fmod (ph, 1.0) < 0.5 ? 1.0f : -1.0f; break;
                    case 5: v = (t % 997 == 0) ? 1.0f : 0.0f; break;
                    default: v = voice[(size_t) (t % (int64_t) voice.size())] * 8.0f; break;   // saturada
                }
                buf.setSample (0, i, v);
                buf.setSample (1, i, (rr() % 2) ? v : -v * 0.5f);
            }
            juce::MidiBuffer midi;
            if (rr() % 6 == 0)
            {
                const int at = (int) (rr() % (unsigned) nn), note = 24 + (int) (rr() % 72);
                switch (rr() % 5)
                {
                    case 0: case 1: midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), at); break;
                    case 2: midi.addEvent (juce::MidiMessage::noteOff (1, note), at); break;
                    case 3: midi.addEvent (juce::MidiMessage::pitchWheel (1, (int) (rr() % 16384)), at); break;
                    default: midi.addEvent (juce::MidiMessage::allNotesOff (1), at); break;
                }
            }
            if (rr() % 50 == 0) p.processBlockBypassed (buf, midi); else p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < nn; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) finite = false; else peak = std::max (peak, std::abs (v));
                }
            pos += nn; ++blocks;
        }
        std::printf ("automatización ao chou: %d bloques, pico %.2f\n", blocks, peak);
        check ("automatización ao chou: sempre finito e sen disparar", finite && peak < 64.0f);
    }

    if (wavOut.isNotEmpty())
    {
        const juce::File f { wavOut };
        writeWav (f, robot);
        writeWav (f.getSiblingFile (f.getFileNameWithoutExtension() + "_natural.wav"), nat);
        writeWav (f.getSiblingFile (f.getFileNameWithoutExtension() + "_mais5.wav"), tr);
    }
    std::printf ("huella da saída: %016llx\n", (unsigned long long) outHash);
    if (jsonOut.isNotEmpty())
    {
        cpuBench (5.0);
        metric ("fallos", fails);
        writeJson (juce::File::getCurrentWorkingDirectory().getChildFile (jsonOut));
    }
    std::printf (fails == 0 ? "TODO OK\n" : "%d FALLOS\n", fails);
    return fails;
}
