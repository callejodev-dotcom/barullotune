#include "Edicion.h"

namespace
{
    void atomicMin (std::atomic<int>& a, int v) { int o = a.load(); while (v < o && ! a.compare_exchange_weak (o, v)) {} }
    void atomicMax (std::atomic<int>& a, int v) { int o = a.load(); while (v > o && ! a.compare_exchange_weak (o, v)) {} }
    constexpr int16_t nada = -2, semVoz = -1;
}

Edicion::Edicion()
    : capturado (new std::atomic<int16_t>[(size_t) maxTramas]), forzado (new std::atomic<int32_t>[(size_t) maxTramas])
{
    for (int k = 0; k < maxTramas; ++k) { capturado[(size_t) k].store (nada); forzado[(size_t) k].store (0); }
}

// ---- fío de audio
void Edicion::capturar (double t, double midi, bool conVoz)
{
    if (! capturando.load (std::memory_order_relaxed)) { ultimaCapturada = -1; return; }
    const int k = (int) std::floor (t / trama);
    if (k < 0 || k >= maxTramas) { ultimaCapturada = -1; return; }
    const int16_t v = conVoz ? (int16_t) std::max (0, std::min (12799, (int) std::lround (midi * 100.0))) : semVoz;
    // a 48 kHz chega unha trama cada 5,3 ms: énchense as do medio, para que non queden ocos
    int a = k;
    if (ultimaCapturada >= 0 && k > ultimaCapturada && k - ultimaCapturada <= 4) a = ultimaCapturada + 1;
    for (int j = a; j <= k; ++j) capturado[(size_t) j].store (v, std::memory_order_relaxed);
    ultimaCapturada = k;
    atomicMin (primeira, a); atomicMax (ultima, k);
    atomicMin (sucioIni, a); atomicMax (sucioFin, k);
}

// ---- interface
bool Edicion::ton (int k, double& midi) const
{
    if (k < 0 || k >= maxTramas) return false;
    const int16_t v = capturado[(size_t) k].load (std::memory_order_relaxed);
    if (v < 0) return false;
    midi = v * 0.01;
    return true;
}

int Edicion::destinoPorDefecto (double centro, const Permitida& permitida)
{
    const int c = (int) std::lround (centro);
    int mellor = -1; double md = 1e9;
    for (int n = c - 7; n <= c + 7; ++n)
        if (n >= 0 && n < 128 && (! permitida || permitida (n)) && std::abs (centro - n) < md) { md = std::abs (centro - n); mellor = n; }
    return mellor >= 0 ? mellor : std::max (0, std::min (127, c));
}

const Edicion::Nota* Edicion::buscar (uint32_t id) const
{
    for (const auto& n : notas) if (n.id == id) return &n;
    return nullptr;
}

void Edicion::escribirForzado (const Nota& n)
{
    const int c = std::max (-499, std::min (499, (int) std::lround (n.cents)));
    const int32_t v = 1 + n.destino * 1000 + (c + 500);
    for (int k = std::max (0, n.ini); k < std::min (maxTramas, n.fin); ++k) forzado[(size_t) k].store (v, std::memory_order_relaxed);
}

void Edicion::borrarForzado (const Nota& n)
{
    for (int k = std::max (0, n.ini); k < std::min (maxTramas, n.fin); ++k) forzado[(size_t) k].store (0, std::memory_order_relaxed);
}

// O ton capturado, en notas: un tramo con voz é unha nota mentres non se afasta máis de
// 0,35 semitonos do seu centro durante 20 ms (o vibrato e os adornos quedan dentro); un oco
// sen voz de 20 ms ou máis acábaa. As notas de menos de 60 ms xúntanse coa do lado.
bool Edicion::actualizar (const Permitida& permitida)
{
    int a = sucioIni.exchange (maxTramas), b = sucioFin.exchange (-1);
    if (a > b) return false;
    // ata as notas que tocan o tramo e os bordes da voz (para partir tramos enteiros)
    double m = 0;
    for (const auto& n : notas) if (n.fin > a && n.ini <= b) { a = std::min (a, n.ini); b = std::max (b, n.fin - 1); }
    while (a > 0 && ton (a - 1, m)) --a;
    while (b + 1 < maxTramas && ton (b + 1, m)) ++b;
    // fóra as notas do tramo (unha captura nova pisa a vella)
    for (auto it = notas.begin(); it != notas.end();)
    {
        if (it->fin > a && it->ini <= b)
        {
            if (it->editada) { borrarForzado (*it); numEditadas.fetch_sub (1); }
            it = notas.erase (it);
        }
        else ++it;
    }
    // o ton, suavizado (mediana de 5 entre as tramas con voz)
    const int len = b - a + 1;
    std::vector<double> crudo ((size_t) len, -1.0), suave ((size_t) len, -1.0);
    for (int j = 0; j < len; ++j) if (ton (a + j, m)) crudo[(size_t) j] = m;
    for (int j = 0; j < len; ++j)
    {
        if (crudo[(size_t) j] < 0) continue;
        double w[5]; int c = 0;
        for (int d = -2; d <= 2; ++d) { const int q = j + d; if (q >= 0 && q < len && crudo[(size_t) q] >= 0) w[c++] = crudo[(size_t) q]; }
        std::sort (w, w + c);
        suave[(size_t) j] = w[c / 2];
    }
    std::vector<std::pair<int, int>> tramos;   // [ini, fin) relativos a «a»
    // (a nota compárase co seu centro, a media do que leva: así unha voz desafinada con
    // vibrato non se parte, e un cambio de nota, aínda que sexa de medio semitono, si. Nos
    // primeiros 60 ms dunha nota non se parte: a voz aínda está chegando a ela; despois, o
    // centro empeza nas últimas 30 ms)
    bool dentro = false; int ini = 0, oco = 0, fora = 0, gracia = 0; double suma = 0; int conta = 0;
    auto centroDesde = [&] (int j)
    {
        suma = 0; conta = 0;
        for (int q = std::max (ini, j - 5); q <= j; ++q) if (suave[(size_t) q] >= 0) { suma += suave[(size_t) q]; ++conta; }
    };
    for (int j = 0; j < len; ++j)
    {
        const double v = suave[(size_t) j];
        if (v < 0)
        {
            if (dentro && ++oco >= 4) { tramos.push_back ({ ini, j - oco + 1 }); dentro = false; }
            continue;
        }
        oco = 0;
        if (! dentro) { dentro = true; ini = j; gracia = 12; fora = 0; continue; }
        if (gracia > 0) { if (--gracia == 0) centroDesde (j); continue; }
        if (std::abs (v - suma / conta) > 0.35)
        {
            if (++fora >= 4)
            {
                // o corte, ao comezo do paso dunha nota á outra (non no medio): a última
                // trama aínda na nota de antes (a menos de 0,15), como moito 40 ms atrás
                const double antes = suma / conta;
                int corte = j - fora + 1;
                for (int q = corte - 1; q > ini + 4 && q >= corte - 8; --q)
                {
                    if (suave[(size_t) q] < 0 || std::abs (suave[(size_t) q] - antes) <= 0.15) break;
                    corte = q;
                }
                tramos.push_back ({ ini, corte });
                ini = corte; fora = 0; gracia = 12;
            }
        }
        else { fora = 0; suma += v; ++conta; }
    }
    if (dentro) tramos.push_back ({ ini, len - oco });
    // as curtas (menos de 60 ms: o paso dunha nota a outra, adornos), coa seguinte se se
    // tocan (o paso é da nota á que se chega), se non coa de antes, ou fóra
    const int minimo = 12;
    for (size_t i = 0; i < tramos.size();)
    {
        auto& t = tramos[i];
        if (t.second - t.first >= minimo) { ++i; continue; }
        const bool antes = i > 0 && tramos[i - 1].second == t.first;
        const bool despois = i + 1 < tramos.size() && tramos[i + 1].first == t.second;
        if (despois) tramos[i + 1].first = t.first;
        else if (antes) tramos[i - 1].second = t.second;
        tramos.erase (tramos.begin() + (std::ptrdiff_t) i);
    }
    for (const auto& t : tramos)
    {
        Nota n;
        n.id = seguinteId++;
        n.ini = a + t.first; n.fin = a + t.second;
        std::vector<double> v;
        for (int j = t.first; j < t.second; ++j) if (crudo[(size_t) j] >= 0) v.push_back (crudo[(size_t) j]);
        if (v.empty()) continue;
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) (v.size() / 2), v.end());
        n.centro = v[v.size() / 2];
        n.destino = destinoPorDefecto (n.centro, permitida);
        notas.push_back (n);
    }
    std::sort (notas.begin(), notas.end(), [] (const Nota& x, const Nota& y) { return x.ini < y.ini; });
    return true;
}

void Edicion::redestinar (const Permitida& permitida)
{
    for (auto& n : notas) if (! n.editada) { n.destino = destinoPorDefecto (n.centro, permitida); n.cents = 0; }
}

void Edicion::editar (uint32_t id, int destino, double cents, const Permitida&)
{
    for (auto& n : notas)
        if (n.id == id)
        {
            if (! n.editada) numEditadas.fetch_add (1);
            n.editada = true;
            n.destino = std::max (0, std::min (127, destino));
            n.cents = std::max (-50.0, std::min (50.0, cents));
            escribirForzado (n);
            return;
        }
}

void Edicion::restaurar (uint32_t id, const Permitida& permitida)
{
    for (auto& n : notas)
        if (n.id == id)
        {
            if (n.editada) { borrarForzado (n); numEditadas.fetch_sub (1); }
            n.editada = false;
            n.destino = destinoPorDefecto (n.centro, permitida);
            n.cents = 0;
            return;
        }
}

void Edicion::borrar()
{
    for (auto& n : notas) if (n.editada) borrarForzado (n);
    notas.clear();
    numEditadas = 0;
    const int a = std::max (0, primeira.load()), b = std::min (maxTramas - 1, ultima.load());
    for (int k = a; k <= b; ++k) capturado[(size_t) k].store (nada, std::memory_order_relaxed);
    primeira = maxTramas; ultima = -1;
    sucioIni = maxTramas; sucioFin = -1;
}

// ---- estado: o ton capturado (cents en int16, base64) e as notas
juce::ValueTree Edicion::gardar() const
{
    if (baleira()) return {};
    juce::ValueTree t ("EDICION");
    const int a = primeira.load(), b = ultima.load();
    juce::MemoryBlock mb ((size_t) (b - a + 1) * 2);
    auto* d = static_cast<uint8_t*> (mb.getData());
    for (int k = a; k <= b; ++k)
    {
        const auto v = (uint16_t) capturado[(size_t) k].load (std::memory_order_relaxed);
        d[(size_t) (k - a) * 2] = (uint8_t) (v & 0xff); d[(size_t) (k - a) * 2 + 1] = (uint8_t) (v >> 8);
    }
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("ini", a, nullptr);
    t.setProperty ("ton", mb.toBase64Encoding(), nullptr);
    for (const auto& n : notas)
    {
        juce::ValueTree c ("NOTA");
        c.setProperty ("id", (int) n.id, nullptr);
        c.setProperty ("ini", n.ini, nullptr); c.setProperty ("fin", n.fin, nullptr);
        c.setProperty ("centro", n.centro, nullptr);
        c.setProperty ("destino", n.destino, nullptr); c.setProperty ("cents", n.cents, nullptr);
        c.setProperty ("editada", n.editada, nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}

void Edicion::cargar (const juce::ValueTree& t, const Permitida& permitida)
{
    borrar();
    if (! t.hasType ("EDICION")) return;
    // (vén de fóra: todo se comproba)
    const int a = (int) t.getProperty ("ini", -1);
    juce::MemoryBlock mb;
    if (a < 0 || a >= maxTramas || ! mb.fromBase64Encoding (t.getProperty ("ton").toString())) return;
    const int len = std::min ((int) (mb.getSize() / 2), maxTramas - a);
    if (len <= 0) return;
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    for (int j = 0; j < len; ++j)
    {
        int v = (int16_t) (uint16_t) (d[(size_t) j * 2] | (d[(size_t) j * 2 + 1] << 8));
        if (v > 12799 || v < nada) v = nada;
        capturado[(size_t) (a + j)].store ((int16_t) v, std::memory_order_relaxed);
    }
    primeira = a; ultima = a + len - 1;
    uint32_t maxId = 0;
    for (auto c : t)
    {
        if (! c.hasType ("NOTA")) continue;
        Nota n;
        n.id = (uint32_t) (int) c.getProperty ("id", 0);
        n.ini = (int) c.getProperty ("ini", 0); n.fin = (int) c.getProperty ("fin", 0);
        if (n.id == 0 || n.ini < a || n.fin <= n.ini || n.fin > a + len) continue;
        n.centro = std::max (0.0, std::min (127.0, (double) c.getProperty ("centro", 60.0)));
        n.destino = std::max (0, std::min (127, (int) c.getProperty ("destino", 60)));
        n.cents = std::max (-50.0, std::min (50.0, (double) c.getProperty ("cents", 0.0)));
        n.editada = (bool) c.getProperty ("editada", false);
        if (! n.editada) n.destino = destinoPorDefecto (n.centro, permitida);
        maxId = std::max (maxId, n.id);
        notas.push_back (n);
    }
    std::sort (notas.begin(), notas.end(), [] (const Nota& x, const Nota& y) { return x.ini < y.ini; });
    for (const auto& n : notas) if (n.editada) { escribirForzado (n); numEditadas.fetch_add (1); }
    seguinteId = maxId + 1;
}
