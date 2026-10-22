#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// Edición: o ton cantado, capturado sobre a liña de tempo do DAW, partido en notas, e as
// notas que se editan á man. Ao volver tocar ese anaco, cada nota editada vai a onde se
// puxo (o motor corríxea coma sempre, coa velocidade e a transición de Corrección, pero
// enteira: sen tolerancia, flexibilidade nin humanizar). Sen ARA: vale en calquera DAW.
//
// O tempo vai en tramas de 5 ms desde o comezo da liña de tempo (independente da
// frecuencia de mostraxe). O fío de audio escribe o ton capturado e le as notas editadas
// (atómicos, sen bloqueos nin reservas); a interface parte en notas e edita.
class Edicion
{
public:
    static constexpr double trama = 0.005;              // segundos
    static constexpr int maxTramas = 30 * 60 * 200;     // media hora
    // atraso da detección: o ton que dá o detector é o de hai uns 20 ms (a xanela)
    static constexpr double atrasoDeteccion = 0.020;

    Edicion();

    // ---- fío de audio
    // o ton na posición t (segundos da liña de tempo); captura só se está acesa
    void capturar (double t, double midi, bool conVoz);
    void finCaptura() { ultimaCapturada = -1; }        // o DAW parou ou saltou: non encher o oco
    // a nota editada en t: −1 se non hai (e entón cents non se toca)
    int forzada (double t, double& cents) const
    {
        const int k = (int) std::floor (t / trama);
        if (k < 0 || k >= maxTramas) return -1;
        const int32_t v = forzado[(size_t) k].load (std::memory_order_relaxed);
        if (v == 0) return -1;
        cents = (double) ((v - 1) % 1000) - 500.0;
        return (v - 1) / 1000;
    }
    bool hayEditadas() const { return numEditadas.load (std::memory_order_relaxed) > 0; }

    std::atomic<bool> capturando { false };
    std::atomic<double> posicion { -1.0 };   // onde vai o DAW (segundos; −1 parado ou sen DAW)

    // ---- interface (fío da mensaxe)
    struct Nota
    {
        uint32_t id = 0;
        int ini = 0, fin = 0;        // tramas [ini, fin)
        double centro = 0;           // o ton cantado (mediana, MIDI)
        int destino = 0;             // a nota á que vai
        double cents = 0;            // por riba do destino
        bool editada = false;
    };
    std::vector<Nota> notas;         // ordenadas por ini
    std::atomic<uint32_t> revision { 0 };   // sobe con cada cambio das notas (para repintar)

    // parte en notas o que se capturou de novo; «permitida» di as notas da escala (para o
    // destino por defecto). Devolve se cambiou algo
    using Permitida = std::function<bool (int nota)>;
    bool actualizar (const Permitida&);
    void redestinar (const Permitida&);              // as notas sen editar, á nota da escala de agora
    // o ton capturado nunha trama: false se non hai (sen captura ou sen voz)
    bool ton (int k, double& midi) const;
    int primeiraTrama() const { return primeira.load(); }
    int ultimaTrama() const { return ultima.load(); }
    bool baleira() const { return primeira.load() > ultima.load(); }

    const Nota* buscar (uint32_t id) const;
    // editar unha nota (destino e cents); restaurala (como estaba: sen editar)
    void editar (uint32_t id, int destino, double cents, const Permitida&);
    void restaurar (uint32_t id, const Permitida&);
    void borrar();

    // estado (vai co proxecto)
    juce::ValueTree gardar() const;
    void cargar (const juce::ValueTree&, const Permitida&);

    static int destinoPorDefecto (double centro, const Permitida&);

private:
    void escribirForzado (const Nota&);
    void borrarForzado (const Nota&);
    std::unique_ptr<std::atomic<int16_t>[]> capturado;   // cents (MIDI × 100); −1 sen voz; −2 nada
    std::unique_ptr<std::atomic<int32_t>[]> forzado;     // 0 nada; 1 + destino × 1000 + (cents + 500)
    std::atomic<int> primeira { maxTramas }, ultima { -1 };
    std::atomic<int> sucioIni { maxTramas }, sucioFin { -1 };
    std::atomic<int> numEditadas { 0 };
    int ultimaCapturada = -1;   // (fío de audio)
    uint32_t seguinteId = 1;
};
