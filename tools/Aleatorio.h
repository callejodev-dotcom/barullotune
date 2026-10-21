// Barullo Tune — números aleatorios iguais en todos os sistemas, para as probas.
// O estándar non fixa std::normal_distribution nin std::uniform_real_distribution:
// libc++ (Mac), libstdc++ (Linux) e MSVC dan series distintas da mesma semente, e
// as probas medían un sinal distinto en cada sistema. Estas só usan a saída de
// std::mt19937, que si é a mesma en todas partes, e seguen os algoritmos de
// libstdc++ (así en Linux as medidas non cambian).
#pragma once
#include <cmath>
#include <limits>
#include <random>

namespace aleatorio {

// [0, 1), coma std::generate_canonical con mt19937 (32 bits por chamada)
template <typename T>
inline T canonico (std::mt19937& g)
{
    constexpr int bits = std::numeric_limits<T>::digits;
    constexpr int chamadas = bits > 32 ? (bits + 31) / 32 : 1;
    T suma = 0, escala = 1;
    for (int k = 0; k < chamadas; ++k) { suma += (T) g() * escala; escala *= (T) 4294967296.0; }
    const T r = suma / escala;
    return r >= (T) 1 ? std::nextafter ((T) 1, (T) 0) : r;
}

template <typename T>
struct Uniforme
{
    T a, b;
    Uniforme (T lo, T hi) : a (lo), b (hi) {}
    T operator() (std::mt19937& g) { return canonico<T> (g) * (b - a) + a; }
};

// método polar de Marsaglia, na orde de libstdc++
struct Normal
{
    double media, desv, gardado = 0;
    bool hai = false;
    Normal (double m, double s) : media (m), desv (s) {}
    double operator() (std::mt19937& g)
    {
        double r;
        if (hai) { hai = false; r = gardado; }
        else
        {
            double x, y, r2;
            do
            {
                x = 2.0 * canonico<double> (g) - 1.0;
                y = 2.0 * canonico<double> (g) - 1.0;
                r2 = x * x + y * y;
            } while (r2 > 1.0 || r2 == 0.0);
            const double k = std::sqrt (-2 * std::log (r2) / r2);
            gardado = x * k; hai = true;
            r = y * k;
        }
        return r * desv + media;
    }
};

} // namespace aleatorio
