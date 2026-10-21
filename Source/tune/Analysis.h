// Barullo Tune — análise da voz, común a todas as voces de saída: o anel coa voz de
// entrada, o detector de ton, as marcas de ciclo (centros de grano con precisión de
// fracción de mostra), o ton deslizante e a decisión da nota de destino.
//
// Sen reservas de memoria no fío de audio: todo se reserva en prepare().
#pragma once
#include "NoteDecider.h"
#include "PitchDetector.h"

namespace tune {

class Analysis
{
public:
    explicit Analysis (const Settings& s) : set (s), decider (s, midi) {}

    // tipos de voz: rango do detector (e latencia en estudio)
    enum Range { Bass = 0, Baritone, Tenor, Alto, Mezzo, Soprano, General, numRanges };

    static void rangeHz (int r, double& lo, double& hi)
    {
        //                                    baixo barítono tenor alto mezzo soprano calquera
        static const double mins[numRanges] = {  60,  75,  95, 130,  150,  180,   70 };
        static const double maxs[numRanges] = { 420, 520, 680, 900, 1050, 1350, 1100 };
        r = std::max (0, std::min ((int) numRanges - 1, r));
        lo = mins[r]; hi = maxs[r];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decider.prepare (sr);
        detector.prepare (sr);
        pMaxAlloc = (int) std::ceil (sr / PitchDetector::allocMinHz) + 2;
        ringSize = 1; while (ringSize < 10 * pMaxAlloc + 256) ringSize <<= 1;
        in.assign ((size_t) ringSize, 0.0f);
        for (int c = 0; c < 2; ++c) inCh[c].assign ((size_t) ringSize, 0.0f);
        unvoicedP = std::max (16.0, std::round (sr * 0.005));
        sinc.build();
    }

    // rango da voz: o detector só busca nel
    void setRange (int r)
    {
        double lo, hi;
        rangeHz (r, lo, hi);
        detector.setRange (lo, hi);
        pMax = (int) std::ceil (sr / lo) + 2;
    }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        for (int c = 0; c < 2; ++c) std::fill (inCh[c].begin(), inCh[c].end(), 0.0f);
        detector.reset();
        n = 0;
        nMarks = 0; firstMark = 0; lastMark = 0; lastMarkVoiced = false;
        cycleOk = false;
        slN = 0; slFirst = 0; slTau = 0; slCount = 0; slSlope = 0;
        target = -1; decider.reset();
        voiced = false; detectedHz = 0; correctionCents = 0; inputMidi = 0; outputMidi = 0;
    }

    // ---- MIDI ----
    MidiState midi;

    // unha mostra de 1 ou 2 canles: o ton detéctase no centro (L+R)/2
    void push (const float* x, int numChannels)
    {
        nch = numChannels >= 2 ? 2 : 1;
        const size_t w = (size_t) (n & (ringSize - 1));
        const float mid = nch == 1 ? x[0] : 0.5f * (x[0] + x[1]);
        in[w] = mid;
        for (int c = 0; c < nch; ++c) inCh[c][w] = x[c];
        if (detector.push (mid)) updatePitch();
        placeMarks();   // todos os xeitos seguen os ciclos da voz
    }
    void advance() { ++n; }

    float read (const std::vector<float>& buf, double p) const { return sinc.read (buf, ringSize, p); }

    bool isFree (int note) const { return noteFree (set, midi, note); }

    // pitch bend: só no modo MIDI nota e mentres hai unha nota tocada
    double bendNow() const { return (set.mode == Settings::MidiNote && midi.lastNote >= 0) ? midi.bend() * set.bendRange : 0.0; }

    // ============================================================ marcas de ciclo
    struct Mark { double pos; double period; bool voiced; int target; };
    static constexpr int maxMarks = 512, maxSearch = 512;

    const Mark& markAt (int k) const { return marks[(size_t) ((firstMark + k) % maxMarks)]; }

    // ton na posición p a partir dos ciclos xa marcados: interpola entre os centros dos
    // ciclos veciños. Devolve false se p aínda non está entre dúas marcas con voz.
    bool pitchAtFromMarks (double p, double& m) const
    {
        int k = nMarks - 1;
        while (k >= 1 && markAt (k).pos > p) --k;   // markAt(k).pos <= p
        if (k < 1 || k + 1 >= nMarks) return false;
        auto periodOf = [this] (int i) -> double     // intervalo entre a marca i e a i+1
        {
            if (i < 0 || i + 1 >= nMarks) return 0;
            const Mark& a = markAt (i); const Mark& b = markAt (i + 1);
            if (! a.voiced || ! b.voiced) return 0;
            const double per = b.pos - a.pos;
            return (per < curP * 0.8 || per > curP * 1.25) ? 0.0 : per;
        };
        const double pk = periodOf (k);
        if (pk <= 0) return false;
        auto toMidi = [this] (double per) { return 69.0 + 12.0 * std::log2 (sr / per / set.refA); };
        const double ck = markAt (k).pos + 0.5 * pk;
        const int nb = p < ck ? k - 1 : k + 1;          // intervalo veciño cara a onde está p
        const double pn = periodOf (nb);
        if (pn <= 0) { m = toMidi (pk); return true; }
        const double cn = markAt (nb).pos + 0.5 * pn;
        const double t = clampd ((p - ck) / (cn - ck), 0.0, 1.0);
        m = toMidi (pk) + (toMidi (pn) - toMidi (pk)) * t;
        return true;
    }

    // período local dun grano: a distancia ás marcas veciñas (máis exacta có detector,
    // que vai uns milisegundos atrasado e cun vibrato rápido erra decenas de cents)
    double localPeriod (int k) const
    {
        const Mark& mk = markAt (k);
        double P = mk.period;
        const bool prev = k > 0 && markAt (k - 1).voiced;
        const bool next = k + 1 < nMarks && markAt (k + 1).voiced;
        if (prev && next) P = 0.5 * (markAt (k + 1).pos - markAt (k - 1).pos);
        else if (prev) P = mk.pos - markAt (k - 1).pos;
        else if (next) P = markAt (k + 1).pos - mk.pos;
        if (P < mk.period * 0.75 || P > mk.period * 1.33) P = mk.period;   // salto raro: fíase do detector
        return P;
    }

    // ============================================================ ton deslizante
    // Para Directo e Preciso: cada poucas mostras mide o período nunha xanela de período e
    // medio que acaba na última mostra (correlación normalizada arredor do período anterior,
    // con parábola). As medidas solápanse, así non teñen o tremor dun ciclo solto, e o seu
    // centro queda moi preto de onde se le: o ton no punto de lectura interpólase entre
    // medidas (ou extrapólase un anaco coa pendente do último período). En voz real erra
    // a metade cás marcas de ciclo extrapoladas.
    void slideUpdate()
    {
        if (! voiced) { slN = 0; slTau = 0; return; }
        const double Pd = curP;
        if (slTau > 0.0 && std::abs (slTau / Pd - 1.0) > 0.2) { slN = 0; slTau = 0; }   // o detector di outra cousa: volve empezar
        const double Pref = slTau > 0.0 ? slTau : Pd;
        const int hop = (int) clampd (Pref / 16.0, 8.0, 48.0);
        if (++slCount < hop) return;   // tamén ao buscar de novo: así non custa máis
        slCount = 0;
        const int W = (int) std::lround (1.5 * Pref);
        if (n < W + 2 * pMaxAlloc) return;
        const int64_t end = n;
        double aa = 1e-20;
        {
            const size_t mask = (size_t) (ringSize - 1);
            for (int k = 0; k < W; ++k) { const double a = in[(size_t) (end - W + 1 + k) & mask]; aa += a * a; }
        }
        double d[64];
        int lo, hi;
        // de medida en medida o período case non cambia: abonda con mirar ±1 (e seguir se fai falta)
        if (slTau > 0.0) { const int t0 = (int) std::lround (slTau); lo = t0 - 1; hi = t0 + 1; }
        else { lo = (int) std::floor (Pd * 0.85); hi = (int) std::ceil (Pd * 1.15); }
        lo = std::max (2, lo); hi = std::min (hi, pMaxAlloc);
        // busca: se o mínimo cae no bordo, segue costa abaixo (ata 8 pasos máis)
        int best = -1; double bv = 1e9;
        auto eval = [&] (int t) { return slDiff (end, W, t, aa); };
        if (hi - lo + 1 <= 64)
        {
            for (int t = lo; t <= hi; ++t) { d[t - lo] = eval (t); if (d[t - lo] < bv) { bv = d[t - lo]; best = t; } }
        }
        else
        {
            for (int t = lo; t <= hi; ++t) { const double v = eval (t); if (v < bv) { bv = v; best = t; } }
        }
        auto at = [&] (int t) { return (t >= lo && t <= hi && hi - lo + 1 <= 64) ? d[t - lo] : eval (t); };
        int steps = 0;
        while (steps < 8 && best > 2 && best < pMaxAlloc)
        {
            const double l = at (best - 1), r = at (best + 1);
            if (l < bv) { bv = l; --best; } else if (r < bv) { bv = r; ++best; } else break;
            ++steps;
        }
        if (bv > 0.4 || best <= 2 || best >= pMaxAlloc) { slN = 0; slTau = 0; return; }   // pouco periódico
        const double y0 = at (best - 1), y1 = bv, y2 = at (best + 1), den = y0 - 2.0 * y1 + y2;
        const double tau = best + (den > 0.0 ? clampd (0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0);
        slTau = tau;
        const SlEntry e { (double) end - 0.5 * (W - 1) - 0.5 * tau, 69.0 + 12.0 * std::log2 (sr / tau / set.refA) };
        if (slN > 0 && e.c <= slAt (slN - 1).c) return;
        slH[(slFirst + slN) % slMax] = e;
        if (slN < slMax) ++slN; else slFirst = (slFirst + 1) % slMax;
        // tendencia: recta (ou parábola) polas medidas do último anaco, ancorada na última
        slFit (e.c, tau);
    }

    double slWin = 0.75, slMaxSlope = 90.0; int slDeg = 1, slUseFit = 0;   // (axustados con voces reais: tendencia dos últimos 3/4 de período, ata 90 semitonos/s)
    double slBase = 0, slQuad = 0;
    void slFit (double cLast, double tau)
    {
        slSlope = 0; slQuad = 0; slBase = slAt (slN - 1).m;
        int cnt = 0;
        for (int k = slN - 1; k >= 0 && slAt (k).c > cLast - slWin * tau; --k) ++cnt;
        if (cnt < 3) return;
        // mínimos cadrados en h = c − cLast (en períodos, para que os números sexan cómodos)
        double S[5] = {}, T[3] = {};
        for (int k = slN - cnt; k < slN; ++k)
        {
            const double h = (slAt (k).c - cLast) / tau, m = slAt (k).m;
            double hp = 1;
            for (int j = 0; j < 5; ++j) { S[j] += hp; if (j < 3) T[j] += hp * m; hp *= h; }
        }
        double a = 0, b = 0, q = 0;
        if (slDeg == 2 && cnt >= 5)
        {
            // [S0 S1 S2; S1 S2 S3; S2 S3 S4] [a b q] = [T0 T1 T2]
            const double A[3][3] = { { S[0], S[1], S[2] }, { S[1], S[2], S[3] }, { S[2], S[3], S[4] } };
            const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
            if (std::abs (det) < 1e-12) return;
            auto d3 = [&] (int col) { double M[3][3]; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] = j == col ? T[i] : A[i][j];
                                      return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]); };
            a = d3 (0) / det; b = d3 (1) / det; q = d3 (2) / det;
        }
        else
        {
            const double den = S[0] * S[2] - S[1] * S[1];
            if (std::abs (den) < 1e-12) return;
            a = (T[0] * S[2] - S[1] * T[1]) / den; b = (S[0] * T[1] - S[1] * T[0]) / den;
        }
        const double maxS = slMaxSlope / sr * tau;   // semitonos por período
        slSlope = clampd (b, -maxS, maxS) / tau;
        slQuad = q / (tau * tau);
        if (slUseFit) slBase = a;
    }

    bool pitchAtSliding (double p, double& m) const
    {
        if (slN == 0) return false;
        const SlEntry& last = slAt (slN - 1);
        if (p >= last.c) { const double h = std::min (p - last.c, 4.0 * slTau); m = slBase + slSlope * h + slQuad * h * h; return true; }
        const SlEntry& first = slAt (0);
        if (p < first.c) { if (p < first.c - 2.0 * slTau) return false; m = first.m; return true; }
        int lo = 0, hi = slN - 1;   // slAt(lo).c <= p < slAt(hi).c
        while (hi - lo > 1) { const int mid = (lo + hi) / 2; if (slAt (mid).c <= p) lo = mid; else hi = mid; }
        const SlEntry& a = slAt (lo); const SlEntry& b = slAt (hi);
        m = a.m + (b.m - a.m) * (p - a.c) / std::max (1e-9, b.c - a.c);
        return true;
    }

    // ---- estado público (lido polas voces de saída) ----
    const Settings& set;
    double sr = 48000;
    PitchDetector detector;
    Sinc sinc;
    std::vector<float> in;                // o centro (para detectar e marcar)
    std::vector<float> inCh[2];           // cada canle
    int ringSize = 8192, nch = 1;
    int64_t n = 0;
    int pMax = 640, pMaxAlloc = 800, latencySamples = 64;
    double unvoicedP = 240;

    // ton actual e nota de destino
    bool voiced = false;
    double curP = 200, detectedHz = 0, inputMidi = 0;
    int target = -1;
    // o que sae (escríbeno as voces de saída; para a interface)
    double correctionCents = 0, outputMidi = 0;

    // marcas
    int firstMark = 0, nMarks = 0;
    // ton dos últimos ciclos e a súa tendencia (para extrapolar en Directo)
    double cycleM = 0, cycleT = 0, cycleSlope = 0;
    bool cycleOk = false;

private:

    void updatePitch()
    {
        const bool was = voiced;
        voiced = detector.voiced;
        if (! voiced) { decider.reset(); return; }
        curP = detector.period;
        detectedHz = sr / curP;
        const double m = 69.0 + 12.0 * std::log2 (detectedHz / set.refA);
        if (! was) decider.reset();
        target = decider.update (m, detector.hopSeconds() * sr);
        inputMidi = m;
    }

    // marcas de análise (centros de grano), sincronizadas co ton e con precisión de fracción de mostra
    void placeMarks()
    {
        const double P = voiced ? curP : unvoicedP;
        auto X = [this] (int64_t i) { return in[(size_t) (i & (ringSize - 1))]; };
        for (;;)
        {
            const int64_t last = (int64_t) std::floor (lastMark);
            const double lastFrac = lastMark - (double) last;
            int64_t c = (nMarks == 0) ? n - (int64_t) (2 * P) : last + (int64_t) std::lround (P);
            const int search = voiced ? (int) (P * (lastMarkVoiced ? 0.2 : 0.5)) : 0;
            // para comparar fai falta medio ciclo despois da marca (o grano enteiro xa o mira estudio ao usala)
            if (c + search + std::max<int64_t> (8, (int64_t) (P * 0.5)) + 1 > n) break;
            double pos = (double) c;
            if (search > 0 && lastMarkVoiced && nMarks > 0)
            {
                // a marca nova vai ao mesmo punto do ciclo cá anterior: o desprazamento co que
                // o ciclo seguinte se parece máis ao actual (correlación normalizada + parábola)
                const int h = std::max (8, (int) (P * 0.5));
                const int nS = std::min (2 * search + 1, maxSearch);
                double best = -1e30; int bi = 0;
                for (int j = 0; j < nS; ++j)
                {
                    const int64_t i = c - search + j;
                    double ab = 0, bb = 1e-12;
                    for (int k = -h; k <= h; ++k) { const double x0 = X (last + k), x1 = X (i + k); ab += x0 * x1; bb += x1 * x1; }
                    score[j] = ab / std::sqrt (bb);
                    if (score[j] > best) { best = score[j]; bi = j; }
                }
                double frac = 0;
                if (bi > 0 && bi < nS - 1)
                {
                    const double y0 = score[bi - 1], y1 = score[bi], y2 = score[bi + 1], den = y0 - 2 * y1 + y2;
                    if (den < 0) frac = clampd (0.5 * (y0 - y2) / den, -0.5, 0.5);
                }
                pos = (double) (c - search + bi) + frac + lastFrac;
                pos = std::max (pos, lastMark + P * 0.6);
            }
            else if (search > 0)
            {
                // primeira marca dunha nota: no pico máis alto
                int64_t bestI = c; float bv = -1e9f;
                for (int64_t i = c - search; i <= c + search; ++i)
                {
                    const float v = std::abs (X (i));
                    if (v > bv) { bv = v; bestI = i; }
                }
                pos = (double) bestI;
                if (nMarks > 0 && pos <= lastMark) pos = lastMark + std::max (1.0, P * 0.5);
            }
            Mark& mk = marks[(size_t) ((firstMark + nMarks) % maxMarks)];
            mk = { pos, P, voiced, target };
            if (nMarks < maxMarks) ++nMarks; else firstMark = (firstMark + 1) % maxMarks;
            lastMark = pos; lastMarkVoiced = voiced;
            updateCycle();
        }
        // tira as marcas vellas
        while (nMarks > 2 && marks[(size_t) firstMark].pos < (double) (n - latencySamples - 4 * pMax))
        { firstMark = (firstMark + 1) % maxMarks; --nMarks; }
    }

    // ton dos dous últimos ciclos medidos e a súa tendencia (para o xeito directo, que
    // le a voz case ao momento e non pode esperar polo detector)
    void updateCycle()
    {
        // recta axustada ao ton dos últimos ciclos (mínimos cadrados): o ton e a tendencia
        // sen o tremor ciclo a ciclo dunha voz real
        cycleOk = false;
        constexpr int K = 3;
        if (nMarks < K + 1) return;
        double t[K], m[K];
        for (int i = 0; i < K; ++i)
        {
            const Mark& a = markAt (nMarks - 1 - i); const Mark& b = markAt (nMarks - 2 - i);
            if (! (a.voiced && b.voiced)) return;
            const double p = a.pos - b.pos;
            if (p < curP * 0.92 || p > curP * 1.09) return;   // máis de semitono e medio do detector: marca dubidosa
            t[i] = 0.5 * (a.pos + b.pos);
            m[i] = 69.0 + 12.0 * std::log2 (sr / p / set.refA);
        }
        double tm = 0, mm = 0;
        for (int i = 0; i < K; ++i) { tm += t[i]; mm += m[i]; }
        tm /= K; mm /= K;
        double num = 0, den = 0;
        for (int i = 0; i < K; ++i) { num += (t[i] - tm) * (m[i] - mm); den += (t[i] - tm) * (t[i] - tm); }
        double res = 0;
        for (int i = 0; i < K; ++i) { const double e = m[i] - (mm + (den > 0 ? num / den : 0.0) * (t[i] - tm)); res += e * e; }
        if (std::sqrt (res / K) > 0.12) return;   // ciclos que non casan (máis de 12 cents fóra da recta): mellor o detector
        const double maxSlope = 25.0 / sr;   // 25 semitonos por segundo, como moito
        cycleSlope = den > 0 ? clampd (num / den, -maxSlope, maxSlope) : 0.0;
        cycleM = mm; cycleT = tm; cycleOk = true;
    }

    // 1 − correlación normalizada entre a xanela [end−W+1, end] e a mesma t mostras antes
    // (aa: enerxía da xanela, común a todos os atrasos)
    double slDiff (int64_t end, int W, int t, double aa) const
    {
        const int64_t a0 = end - W + 1, b0 = a0 - t;
        const size_t mask = (size_t) (ringSize - 1);
        const size_t ia = (size_t) a0 & mask, ib = (size_t) b0 & mask;
        double ab0 = 0, ab1 = 0, bb0 = 1e-20, bb1 = 0;
        if (ia + (size_t) W <= (size_t) ringSize && ib + (size_t) W <= (size_t) ringSize)
        {
            const float* A = in.data() + ia; const float* B = in.data() + ib;   // tramo seguido: o bucle rápido
            int k = 0;
            for (; k + 1 < W; k += 2)
            {
                ab0 += (double) A[k] * B[k];         bb0 += (double) B[k] * B[k];
                ab1 += (double) A[k + 1] * B[k + 1]; bb1 += (double) B[k + 1] * B[k + 1];
            }
            for (; k < W; ++k) { ab0 += (double) A[k] * B[k]; bb0 += (double) B[k] * B[k]; }
        }
        else
            for (int k = 0; k < W; ++k)
            {
                const double a = in[(ia + (size_t) k) & mask], b = in[(ib + (size_t) k) & mask];
                ab0 += a * b; bb0 += b * b;
            }
        return 1.0 - (ab0 + ab1) / std::sqrt (aa * (bb0 + bb1));
    }

    struct SlEntry { double c, m; };
    static constexpr int slMax = 256;
    const SlEntry& slAt (int k) const { return slH[(slFirst + k) % slMax]; }

    SlEntry slH[slMax] {};
    int slFirst = 0, slN = 0, slCount = 0;
    double slTau = 0, slSlope = 0;

    Mark marks[maxMarks] {};
    double lastMark = 0; bool lastMarkVoiced = false;
    double score[maxSearch] {};

    NoteDecider decider;
};

} // namespace tune
