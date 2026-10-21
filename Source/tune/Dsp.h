// Barullo Tune — pezas básicas de sinal: filtro biquad e lectura entre mostras.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace tune {

constexpr double kPi = 3.14159265358979323846;   // M_PI non existe en MSVC

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fc, double sr, double q = 0.7071)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0; a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

static inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// lectura entre mostras: sinc con xanela de Kaiser, 16 puntos (plana ata ~20 kHz).
// A interpolación lineal apagaba os agudos (−3 dB a 14 kHz) e metía un ruído que
// cambiaba co ton.
struct Sinc
{
    static constexpr int half = 8, phases = 512;
    std::vector<float> table;

    void build()
    {
        table.assign ((size_t) (phases + 1) * 2 * half, 0.0f);
        const double fc = 0.47, beta = 8.0;
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double i0b = bessel0 (beta);
        for (int ph = 0; ph <= phases; ++ph)
        {
            const double f = (double) ph / phases;
            double sum = 0;
            float* row = &table[(size_t) ph * 2 * half];
            for (int k = 0; k < 2 * half; ++k)
            {
                const double x = (double) (k - (half - 1)) - f;          // taps -7 … +8
                const double r = x / (double) half;
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                const double v = std::abs (x) < 1e-9 ? 2.0 * fc : std::sin (2.0 * kPi * fc * x) / (kPi * x);
                row[k] = (float) (v * w); sum += v * w;
            }
            for (int k = 0; k < 2 * half; ++k) row[k] = (float) (row[k] / sum);
        }
    }

    // buf é un anel de tamaño potencia de 2
    float read (const std::vector<float>& buf, int ringSize, double p) const
    {
        const int64_t i0 = (int64_t) std::floor (p);
        const double fp = (p - (double) i0) * phases;
        const int ph = std::min (phases - 1, (int) fp);
        const float fr = (float) (fp - ph);
        const float* r0 = &table[(size_t) ph * 2 * half];
        const float* r1 = r0 + 2 * half;
        float acc = 0.0f;
        const int64_t base = i0 - (half - 1);
        for (int k = 0; k < 2 * half; ++k)
            acc += (r0[k] + fr * (r1[k] - r0[k])) * buf[(size_t) ((base + k) & (ringSize - 1))];
        return acc;
    }
};

} // namespace tune
