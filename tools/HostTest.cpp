// Barullo Tune nun DAW de proba: carga o VST3 de verdade (o mesmo binario que se instala) e
// fai co transporte o que fai un DAW: tocar desde o comezo, parar, volver tocar, bloques de
// calquera tamaño, render sen tempo real (conxelar ou exportar) e pechar e abrir o proxecto.
//
// Coa edición: captura unha toma (co parámetro «Edición: capturar», coma o botón ou unha
// automatización), edita unha nota no estado (coma ao abrir un proxecto editado), volve tocar
// e mide o ton que sae; abre outra instancia co estado gardado e mide outra vez; e en Estudio,
// coa súa latencia.
//
// Uso: BTHostTest <ruta ao «Barullo Tune.vst3»>
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cstdio>
#include <random>

namespace
{
constexpr double SR = 48000.0;

// a melodía: seis notas un pouco altas (+0,3 semitonos), con vibrato suave e silencios entre elas
struct Nota { double ini, fin, midi; };
std::vector<Nota> melodia()
{
    std::vector<Nota> m;
    const double base[] = { 60.3, 62.3, 64.3, 65.3, 67.3, 69.3 };
    double t = 0.5;
    for (double b : base) { m.push_back ({ t, t + 0.55, b }); t += 0.7; }
    return m;
}

juce::AudioBuffer<float> voz (double& duracion)
{
    const auto mel = melodia();
    duracion = mel.back().fin + 0.5;
    const int n = (int) (duracion * SR);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    double fase = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        double midi = -1, env = 0;
        for (const auto& nt : mel)
            if (t >= nt.ini && t < nt.fin)
            {
                midi = nt.midi + 0.08 * std::sin (2 * juce::MathConstants<double>::pi * 5.2 * (t - nt.ini));
                env = std::min ({ 1.0, (t - nt.ini) / 0.02, (nt.fin - t) / 0.02 });
            }
        if (midi < 0) continue;
        const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
        fase += 2 * juce::MathConstants<double>::pi * f / SR;
        double x = 0;
        for (int k = 1; k <= 14; ++k) x += std::sin (k * fase) / k;
        const float v = (float) (0.18 * env * x);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

// o ton dunha xanela (YIN): MIDI, ou −1
double ton (const float* x, int n)
{
    const int tmin = 30, tmax = 700;
    if (n < tmax * 2) return -1;
    const int w = n - tmax;
    std::vector<double> d ((size_t) tmax + 1, 0.0);
    for (int tau = 1; tau <= tmax; ++tau)
    {
        double s = 0;
        for (int i = 0; i < w; ++i) { const double e = x[i] - x[i + tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double acum = 0;
    std::vector<double> c ((size_t) tmax + 1, 1.0);
    for (int tau = 1; tau <= tmax; ++tau) { acum += d[(size_t) tau]; c[(size_t) tau] = acum > 0 ? d[(size_t) tau] * tau / acum : 1.0; }
    for (int tau = tmin; tau < tmax; ++tau)
        if (c[(size_t) tau] < 0.12 && c[(size_t) tau] <= c[(size_t) tau + 1])
        {
            const double a = c[(size_t) tau - 1], b = c[(size_t) tau], cc = c[(size_t) tau + 1];
            const double den = a - 2 * b + cc;
            const double t = tau + (std::abs (den) > 1e-12 ? 0.5 * (a - cc) / den : 0.0);
            return 69.0 + 12.0 * std::log2 (SR / t / 440.0);
        }
    return -1;
}

// a mediana do ton entre dous tempos (segundos), deixando fóra os bordos
double tonEntre (const juce::AudioBuffer<float>& b, double a, double z)
{
    std::vector<double> v;
    for (double t = a + 0.12; t + 0.04 < z - 0.06; t += 0.02)
    {
        const int i = (int) (t * SR);
        if (i + 2048 >= b.getNumSamples()) break;
        const double m = ton (b.getReadPointer (0) + i, 2048);
        if (m > 0) v.push_back (m);
    }
    if (v.empty()) return -1;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

struct Cabezal : juce::AudioPlayHead
{
    int64_t mostra = 0;
    bool tocando = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (tocando);
        p.setTimeInSamples (mostra);
        p.setTimeInSeconds ((double) mostra / SR);
        p.setBpm (120.0);
        p.setPpqPosition ((double) mostra / SR * 2.0);
        return p;
    }
};

juce::AudioProcessorParameter* parametro (juce::AudioPluginInstance& p, const juce::String& nome)
{
    for (auto* prm : p.getParameters()) if (prm->getName (128) == nome) return prm;
    return nullptr;
}

void pon (juce::AudioPluginInstance& p, const juce::String& nome, float v01)
{
    if (auto* prm = parametro (p, nome)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (v01); prm->endChangeGesture(); }
    else std::printf ("  (non atopo o parámetro «%s»)\n", nome.toRawUTF8());
}

void mensaxes (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// tocar a voz enteira desde o comezo (o DAW: play), en bloques de «bloque» (0 = ao chou)
juce::AudioBuffer<float> tocar (juce::AudioPluginInstance& p, Cabezal& cab, const juce::AudioBuffer<float>& entrada, int bloque, bool semTempoReal = false)
{
    const int n = entrada.getNumSamples();
    juce::AudioBuffer<float> saida (2, n);
    juce::AudioBuffer<float> buf (2, 4096);
    p.setNonRealtime (semTempoReal);
    cab.tocando = true;
    std::mt19937 rr (7);
    int pos = 0;
    while (pos < n)
    {
        const int b = std::min (n - pos, bloque > 0 ? bloque : 1 + (int) (rr() % 2048));
        cab.mostra = pos;
        juce::AudioBuffer<float> vista (buf.getArrayOfWritePointers(), 2, b);
        for (int c = 0; c < 2; ++c) vista.copyFrom (c, 0, entrada, c, pos, b);
        juce::MidiBuffer m;
        p.processBlock (vista, m);
        for (int c = 0; c < 2; ++c) saida.copyFrom (c, pos, vista, c, 0, b);
        pos += b;
        if ((pos / b) % 32 == 0) mensaxes (1);   // (o DAW atende a interface mentres toca)
    }
    cab.tocando = false;
    cab.mostra = n;
    juce::AudioBuffer<float> silencio (2, 512);
    for (int k = 0; k < 4; ++k) { silencio.clear(); juce::MidiBuffer m; p.processBlock (silencio, m); }   // parado
    p.setNonRealtime (false);
    return saida;
}

// O estado: o DAW garda o do VST3 (<VST3PluginState><IComponent>base64</IComponent>…) e dentro
// vai o do plugin (o formato de copyXmlToBinary) seguido dos datos propios do envoltorio de JUCE
struct Estado
{
    std::unique_ptr<juce::XmlElement> vst3, plugin;
    juce::MemoryBlock resto;   // o que vai detrás do estado do plugin (do envoltorio)
};

Estado estadoXml (juce::AudioPluginInstance& p)
{
    Estado e;
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    e.vst3 = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (e.vst3 == nullptr) return e;
    if (auto* comp = e.vst3->getChildByName ("IComponent"))
    {
        juce::MemoryBlock c;
        if (c.fromBase64Encoding (comp->getAllSubText().trim()) && c.getSize() > 8)
        {
            const auto len = (size_t) juce::ByteOrder::littleEndianInt (juce::addBytesToPointer (c.getData(), 4));
            e.plugin = juce::AudioProcessor::getXmlFromBinary (c.getData(), (int) c.getSize());
            if (8 + len < c.getSize()) e.resto = juce::MemoryBlock (juce::addBytesToPointer (c.getData(), 8 + len), c.getSize() - 8 - len);
        }
    }
    return e;
}

void ponEstado (juce::AudioPluginInstance& p, Estado& e)
{
    juce::MemoryBlock c;
    juce::AudioProcessor::copyXmlToBinary (*e.plugin, c);
    c.append (e.resto.getData(), e.resto.getSize());
    if (auto* comp = e.vst3->getChildByName ("IComponent"))
    {
        comp->deleteAllTextElements();
        comp->addTextElement (c.toBase64Encoding());
    }
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (*e.vst3, mb);
    p.setStateInformation (mb.getData(), (int) mb.getSize());
    mensaxes (300);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("uso: BTHostTest <Barullo Tune.vst3>\n"); return 2; }
    const juce::File vst3 (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> tipos;
    fm.getFormat (0)->findAllTypesForFile (tipos, vst3.getFullPathName());
    if (tipos.isEmpty()) { std::printf ("non atopo o plugin en %s\n", vst3.getFullPathName().toRawUTF8()); return 2; }
    std::printf ("plugin: %s %s (%s)\n", tipos[0]->name.toRawUTF8(), tipos[0]->version.toRawUTF8(), tipos[0]->pluginFormatName.toRawUTF8());

    auto novo = [&] (Cabezal& cab)
    {
        juce::String err;
        auto p = fm.createPluginInstance (*tipos[0], SR, 512, err);
        if (p == nullptr) { std::printf ("non se crea: %s\n", err.toRawUTF8()); std::exit (2); }
        p->setPlayHead (&cab);
        p->prepareToPlay (SR, 4096);
        return p;
    };

    int fallos = 0;
    auto proba = [&] (const char* que, bool ok) { std::printf ("%-60s %s\n", que, ok ? "OK" : "FALLO"); fallos += ok ? 0 : 1; };

    double dur = 0;
    const auto entrada = voz (dur);
    const auto mel = melodia();

    // ---- 1. Directo: capturar a toma
    Cabezal cab;
    auto p = novo (cab);
    pon (*p, juce::String::fromUTF8 ("Edición: capturar"), 1.0f);
    const auto enDirecto = tocar (*p, cab, entrada, 512);
    if (std::getenv ("BT_DEBUG"))
    {
        double dif = 0; for (int i = 0; i < enDirecto.getNumSamples(); ++i) dif += std::abs (enDirecto.getSample (0, i) - entrada.getSample (0, i));
        auto* c = parametro (*p, juce::String::fromUTF8 ("Edición: capturar"));
        std::printf ("  [debug: diferenza saída-entrada %.3f; capturar %.2f; parámetros %d; entradas %d saídas %d]\n", dif, c ? c->getValue() : -1.0f,
                     p->getParameters().size(), p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        auto xx = estadoXml (*p); if (xx.plugin) std::printf ("  [debug: estado do plugin %s, fillos %d, resto %d bytes]\n", xx.plugin->getTagName().toRawUTF8(), xx.plugin->getNumChildElements(), (int) xx.resto.getSize());
    }
    pon (*p, juce::String::fromUTF8 ("Edición: capturar"), 0.0f);
    mensaxes (500);   // o plugin parte a toma en notas (tamén coa xanela pechada)

    auto x = estadoXml (*p);
    auto* ed = x.plugin != nullptr ? x.plugin->getChildByName ("EDICION") : nullptr;
    int notas = 0;
    juce::XmlElement* terceira = nullptr;
    if (ed != nullptr)
        for (auto* n : ed->getChildWithTagNameIterator ("NOTA"))
        {
            ++notas;
            if (std::abs (n->getDoubleAttribute ("centro") - 64.3) < 0.3) terceira = n;
        }
    std::printf ("captura: %d notas no estado (a melodía ten %zu)\n", notas, mel.size());
    proba ("captura no DAW: a toma, partida en notas e gardada co proxecto", notas >= (int) mel.size() && notas <= (int) mel.size() + 2 && terceira != nullptr);
    if (terceira == nullptr) { std::printf ("%d FALLOS\n", fallos); return 1; }

    // ---- 2. a terceira nota (Mi +30) editada a Fa♯, coma nun proxecto editado; volver tocar
    terceira->setAttribute ("destino", 66);
    terceira->setAttribute ("cents", 0.0);
    terceira->setAttribute ("editada", 1);
    ponEstado (*p, x);
    const auto editada = tocar (*p, cab, entrada, 512);
    const double t3 = tonEntre (editada, mel[2].ini, mel[2].fin), t4 = tonEntre (editada, mel[3].ini, mel[3].fin);
    const double l3 = tonEntre (enDirecto, mel[2].ini, mel[2].fin), l4 = tonEntre (enDirecto, mel[3].ini, mel[3].fin);
    std::printf ("Directo: a editada %.2f (ao vivo %.2f), a seguinte %.2f (ao vivo %.2f)\n", t3, l3, t4, l4);
    proba ("Directo: a nota editada vai a onde se puxo", std::abs (t3 - 66.0) < 0.15 && std::abs (l3 - 64.0) < 0.15);
    proba ("Directo: as outras, coma sempre", std::abs (t4 - l4) < 0.05);

    // ---- 3. pechar e abrir o proxecto: outra instancia co estado gardado
    juce::MemoryBlock gardado;
    p->getStateInformation (gardado);
    p.reset();
    Cabezal cab2;
    auto q = novo (cab2);
    q->setStateInformation (gardado.getData(), (int) gardado.getSize());
    mensaxes (300);
    const auto capt = parametro (*q, juce::String::fromUTF8 ("Edición: capturar"));
    const auto reaberto = tocar (*q, cab2, entrada, 512);
    const double r3 = tonEntre (reaberto, mel[2].ini, mel[2].fin);
    std::printf ("proxecto aberto de novo: a editada %.2f\n", r3);
    proba ("proxecto aberto de novo: a edición segue, e capturar apagado", std::abs (r3 - 66.0) < 0.15 && capt != nullptr && capt->getValue() < 0.5f);

    // ---- 4. conxelar ou exportar: sen tempo real e con bloques de calquera tamaño
    const auto render = tocar (*q, cab2, entrada, 0, true);
    const double x3 = tonEntre (render, mel[2].ini, mel[2].fin), x4 = tonEntre (render, mel[3].ini, mel[3].fin);
    std::printf ("render sen tempo real (bloques ao chou): a editada %.2f, a seguinte %.2f\n", x3, x4);
    proba ("render sen tempo real: o mesmo", std::abs (x3 - 66.0) < 0.15 && std::abs (x4 - t4) < 0.05);

    // ---- 5. Estudio (coa súa latencia: o DAW atrasa o resto, aquí mídese co atraso)
    pon (*q, "Xeito", 1.0f);
    mensaxes (100);
    juce::AudioBuffer<float> b512 (2, 512);
    for (int k = 0; k < 8; ++k) { b512.clear(); juce::MidiBuffer m; q->processBlock (b512, m); }
    mensaxes (300);   // (o plugin avisa ao DAW do cambio de latencia)
    const int lat = q->getLatencySamples();
    if (auto* xe = parametro (*q, "Xeito")) std::printf ("  Xeito: %s\n", xe->getCurrentValueAsText().toRawUTF8());
    const auto estudio = tocar (*q, cab2, entrada, 512);
    const double d = lat / SR;
    const double e3 = tonEntre (estudio, mel[2].ini + d, mel[2].fin + d), e4 = tonEntre (estudio, mel[3].ini + d, mel[3].fin + d);
    std::printf ("Estudio (latencia %d mostras, %.1f ms): a editada %.2f, a seguinte %.2f\n", lat, lat * 1000.0 / SR, e3, e4);
    proba ("Estudio: a nota editada vai a onde se puxo", lat > 0 && std::abs (e3 - 66.0) < 0.15 && std::abs (e4 - 65.0) < 0.15);

    std::printf (fallos == 0 ? "TODO OK\n" : "%d FALLOS\n", fallos);
    return fallos == 0 ? 0 : 1;
}
