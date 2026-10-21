// Barullo Tune — corrección de formantes sen latencia (Directo e Preciso).
#pragma once
#include "Dsp.h"

namespace tune {

// ---------------------------------------------------------------- formantes sen latencia
// Ao ler a voz a outra velocidade (Directo e Preciso) os formantes móvense co ton.
// Isto mide a envolvente espectral (predición lineal) da voz orixinal e da lida, cada
// ~5 ms e só co xa escoitado, e coa voz lida fai: branquear coa súa propia envolvente
// e volver colorear coa envolvente orixinal (ou desprazada co mando de formantes).
// Os filtros son de celosía e os coeficientes de reflexión interpólanse mostra a mostra,
// así sempre son estables.
class FormantFix
{
public:
    static constexpr int maxOrder = 40;

    void prepare (double sr)
    {
        order = std::max (16, std::min (maxOrder, (int) std::lround (sr / 1300.0)));
        frame = 1; while (frame < (int) (0.02 * sr)) frame <<= 1;
        hop = frame / 4;
        win.assign ((size_t) frame, 0.0f);
        for (int i = 0; i < frame; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (i + 0.5) / frame));
        lagW.assign ((size_t) maxOrder + 1, 0.0);
        for (int k = 0; k <= maxOrder; ++k) { const double a = 2.0 * 3.14159265358979323846 * 60.0 * k / sr; lagW[(size_t) k] = std::exp (-0.5 * a * a); }
        yRing.assign ((size_t) frame, 0.0f);
        xf.assign ((size_t) frame, 0.0f);
        wf.assign ((size_t) frame, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (yRing.begin(), yRing.end(), 0.0f);
        for (auto* k : { kx0, kx1, ky0, ky1 }) std::fill (k, k + maxOrder + 1, 0.0);
        for (auto& st : stateF) std::fill (std::begin (st), std::end (st), 0.0);
        for (auto& st : stateI) std::fill (std::begin (st), std::end (st), 0.0);
        g0 = g1 = 1.0; pos = 0; count = 0; amount = 0.0; ready = false;
    }

    // a voz lida (centro) entra aquí mostra a mostra; devolve true cando toca analizar
    bool pushY (float v)
    {
        yRing[(size_t) (pos++ & (frame - 1))] = v;
        if (++count >= hop) { count = 0; return true; }
        return false;
    }

    // analiza: xFrame = as últimas «frame» mostras da voz orixinal (xa lidas ao ritmo dos formantes)
    void analyse (const float* xFrame)
    {
        std::copy (kx1, kx1 + order + 1, kx0); std::copy (ky1, ky1 + order + 1, ky0); g0 = g1;
        double ex = 1, ey = 1;
        const bool okx = lpc (xFrame, 0, kx1, ex);
        for (int i = 0; i < frame; ++i) xf[(size_t) i] = yRing[(size_t) ((pos + i) & (frame - 1))];
        const bool oky = lpc (xf.data(), 0, ky1, ey);
        if (! okx || ! oky) { std::fill (kx1, kx1 + order + 1, 0.0); std::fill (ky1, ky1 + order + 1, 0.0); g1 = 1.0; }
        else g1 = std::sqrt (std::max (1e-6, ex) / std::max (1e-6, ey));
        g1 = std::min (4.0, std::max (0.25, g1));
        if (! ready) { std::copy (kx1, kx1 + order + 1, kx0); std::copy (ky1, ky1 + order + 1, ky0); g0 = g1; ready = true; }
        step = 0;
    }

    // filtra unha mostra da canle c: branquea coa envolvente lida e colorea coa orixinal
    float process (int c, float s)
    {
        const double t = (double) step / hop;
        double f = s;
        double* bF = stateF[c];
        // celosía FIR (A_y): f_m = f_{m-1} + k_m b_{m-1}(n-1); b_m(n) = b_{m-1}(n-1) + k_m f_{m-1}
        double bPrev = f;   // b_0(n)
        for (int m = 1; m <= order; ++m)
        {
            const double k = ky0[m] + (ky1[m] - ky0[m]) * t;
            const double fNew = f + k * bF[m - 1];
            const double bNew = bF[m - 1] + k * f;
            bF[m - 1] = bPrev;
            bPrev = bNew;
            f = fNew;
        }
        // celosía IIR (1/A_x)
        double* bI = stateI[c];
        double e = f;
        for (int m = order; m >= 1; --m)
        {
            const double k = kx0[m] + (kx1[m] - kx0[m]) * t;
            e = e - k * bI[m - 1];
            bI[m] = bI[m - 1] + k * e;
        }
        bI[0] = e;
        if (c == 0 && step < hop) ++step;
        const double g = g0 + (g1 - g0) * t;
        const double out = e * g;
        return std::isfinite (out) ? (float) out : 0.0f;
    }

    int frame = 1024, hop = 128, order = 20;
    double amount = 0.0;   // 0 = sen corrección, 1 = corrección completa (para fundir)
    bool ready = false;
    std::vector<float> xf;  // marco de traballo (a orixinal, lida fóra)

private:
    // autocorrelación con xanela + Levinson. k[1..order] e erro relativo e = E/r0
    bool lpc (const float* x, int, double* k, double& e)
    {
        double r[maxOrder + 1];
        double* w = wf.data();
        for (int i = 0; i < frame; ++i) w[i] = (double) x[i] * win[(size_t) i];   // a xanela, unha soa vez
        for (int lag = 0; lag <= order; ++lag)
        {
            double acc = 0;
            for (int i = lag; i < frame; ++i) acc += w[i] * w[i - lag];
            r[lag] = acc * lagW[(size_t) lag];
        }
        if (r[0] < 1e-12) return false;
        r[0] *= 1.0001;
        double a[maxOrder + 1] = { 1.0 }, tmp[maxOrder + 1];
        for (int i = 1; i <= order; ++i) a[i] = 0;
        double err = r[0];
        for (int m = 1; m <= order; ++m)
        {
            double acc = r[m];
            for (int i = 1; i < m; ++i) acc += a[i] * r[m - i];
            const double km = -acc / err;
            if (! (std::abs (km) < 1.0)) return false;
            for (int i = 0; i <= m; ++i) tmp[i] = a[i];
            for (int i = 1; i < m; ++i) a[i] = tmp[i] + km * tmp[m - i];
            a[m] = km;
            k[m] = km;
            err *= (1.0 - km * km);
        }
        e = err / r[0];
        return true;
    }

    std::vector<float> win, yRing;
    std::vector<double> wf;
    std::vector<double> lagW;
    double kx0[maxOrder + 1] {}, kx1[maxOrder + 1] {}, ky0[maxOrder + 1] {}, ky1[maxOrder + 1] {};
    double stateF[2][maxOrder + 1] {}, stateI[2][maxOrder + 1] {};
    double g0 = 1, g1 = 1;
    int64_t pos = 0;
    int count = 0, step = 0;
};

} // namespace tune
