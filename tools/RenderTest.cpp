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
    if (argc > 1 && juce::String (argv[1]) == "--cpu")
    {
        // tempo real por instancia: voz sintética en bucle, estéreo, bloques de 128
        std::vector<double> tr;
        const auto v = makeVoice (tr);
        for (int q = 0; q < 3; ++q)
        {
            TuneProcessor p;
            p.setPlayConfigDetails (2, 2, SR, 128);
            setP (p, "calidade", (float) q);
            p.prepareToPlay (SR, 128);
            juce::AudioBuffer<float> buf (2, 128);
            const int blocks = (int) (20.0 * SR / 128);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < blocks; ++b)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) buf.setSample (c, i, v[(size_t) ((b * 128 + i) % (int) v.size())]);
                juce::MidiBuffer m; p.processBlock (buf, m);
            }
            const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            std::printf ("%s: %.0f veces tempo real (%.2f %% dun núcleo)\n", q == 0 ? "directo" : q == 1 ? "preciso" : "estudio", 20.0 / secs, 100.0 * secs / 20.0);
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") { snapshot (juce::File { juce::String (argv[2]) }); return 0; }

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
        check (q == 0 ? "directo robot: menos de 8 cents de media" : q == 1 ? "preciso robot: menos de 3 cents de media" : "estudio robot: menos de 3 cents de media",
               s.outC < (q == 0 ? 8.0 : 3.0) && s.wrong < s.frames / 30 + 2);
        if (q == 0) check ("directo: latencia de menos de 1 ms", lat < SR * 0.001);
        if (q == 1) check ("preciso: latencia de menos de 13 ms", lat < SR * 0.013);

        // natural
        auto nt = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 60); setP (p, "transicion", 120); }, lat);
        s = evaluate (nt, lat, truth, 0);
        std::printf ("natural: entrada %.1f c -> saída %.1f c (máx %.1f c)\n", s.inC, s.outC, s.maxC);
        check ("natural: corrixe (menos da metade da desafinación)", s.outC < 0.5 * s.inC);

        // vibrato 100 % con velocidade 0: as notas centradas e o vibrato intacto
        {
            auto vb = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); major (p); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 100); }, lat);
            auto sv = evaluate (vb, lat, truth, 0);
            std::printf ("vibrato: saída %.1f c de media (con vibrato), centro %+.1f c\n", sv.outC, sv.signedC);
            check ("vibrato 100 %: conserva o vibrato e centra a nota", sv.outC > 4.0 && std::abs (sv.signedC) < 3.0);
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
        check ("transpor +5: notas certas", s.outC < 8.0 && s.wrong < s.frames / 30 + 2);

        // MIDI nota: unha terceira por riba da melodía
        {
            std::vector<std::pair<int, int>> ev;
            double t = 0;
            for (auto& m : melody()) { const int n = (int) m.midi + 4; ev.push_back ({ (int) (t * SR), n }); ev.push_back ({ (int) ((t + m.dur) * SR) - 48, -n }); t += m.dur; }
            auto mid = run (voice, 512, 2, [&] (TuneProcessor& p) { setq (p); setP (p, "modo", 2); setP (p, "velocidade", 0); setP (p, "transicion", 0); setP (p, "vibrato", 0); }, lat, ev);
            s = evaluate (mid, lat, truth, 4);
            std::printf ("MIDI:    saída %.1f c (máx %.1f c), %d fóra de nota\n", s.outC, s.maxC, s.wrong);
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
        check ("formantes +4: mesma nota e timbre máis brillante", s.outC < 4.0 && c1 > c0 * 1.12);
    }

    // rango: menos latencia en estudio con voz aguda
    {
        int lg = 0, la = 0;
        run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); setP (p, "rango", 0); }, lg);
        run (voice, 512, 2, [&] (TuneProcessor& p) { setP (p, "calidade", 2); setP (p, "rango", 5); }, la);
        std::printf ("estudio: latencia baixo %.1f ms, soprano %.1f ms\n", 1000.0 * lg / SR, 1000.0 * la / SR);
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

    // presets: Robot pon velocidade, transición e vibrato a 0; o preset gárdase co estado
    {
        TuneProcessor a;
        int robot = -1;
        for (int i = 0; i < a.getNumPrograms(); ++i) if (a.getProgramName (i) == "Robot") robot = i;
        a.setCurrentProgram (robot);
        const bool ok1 = robot >= 0 && a.params.getRawParameterValue ("velocidade")->load() < 0.5f
                      && a.params.getRawParameterValue ("transicion")->load() < 0.5f && a.params.getRawParameterValue ("vibrato")->load() < 0.5f;
        juce::MemoryBlock mb; a.getStateInformation (mb);
        TuneProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("presets: %d, Robot = %d\n", a.getNumPrograms(), robot);
        check ("presets: Robot aplícase e gárdase", ok1 && b.getCurrentProgram() == robot);
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
