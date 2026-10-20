#!/usr/bin/env python3
"""Mide a afinación dunha voz antes e despois de Barullo Tune.

  analise.py ton   entrada.wav                 -> imprime a tonalidade estimada (tonica escala)
  analise.py comp  entrada.wav saida.wav [png] -> desafinación, vibrato e gráfico
"""
import sys
import numpy as np
from scipy.io import wavfile

def load(p):
    sr, x = wavfile.read(p)
    x = x.astype(np.float64)
    if x.ndim > 1: x = x.mean(axis=1)
    if np.abs(x).max() > 2: x /= 32768.0 if np.abs(x).max() < 40000 else 2**31
    return sr, x

def yin(x, sr, hop=None, fmin=70, fmax=1000, thr=0.12):
    hop = hop or int(sr * 0.01)
    W = int(sr / fmin) + 2
    T = int(sr / fmin) + 2
    tmin = int(sr / fmax)
    f0 = []; conf = []
    for c in range(0, len(x) - W - T, hop):
        fr = x[c:c + W + T]
        if np.sqrt(np.mean(fr[:W] ** 2)) < 0.004:
            f0.append(np.nan); conf.append(0); continue
        # diferenza por FFT
        a = fr[:W]
        n = 1 << int(np.ceil(np.log2(W + T + W)))
        Fa = np.fft.rfft(a, n); Fb = np.fft.rfft(fr, n)
        corr = np.fft.irfft(np.conj(Fa) * Fb, n)[:T + 1]
        e = np.cumsum(fr ** 2)
        ea = e[W - 1]
        eb = np.array([e[t + W - 1] - (e[t - 1] if t > 0 else 0) for t in range(T + 1)])
        d = ea + eb - 2 * corr
        d[0] = 0
        cm = np.ones_like(d)
        cs = np.cumsum(d[1:])
        cm[1:] = d[1:] * np.arange(1, T + 1) / np.maximum(cs, 1e-12)
        idx = np.where(cm[tmin:T] < thr)[0]
        if len(idx) == 0:
            f0.append(np.nan); conf.append(0); continue
        t = idx[0] + tmin
        while t + 1 < T and cm[t + 1] < cm[t]: t += 1
        den = cm[t - 1] - 2 * cm[t] + cm[t + 1]
        tau = t + (0.5 * (cm[t - 1] - cm[t + 1]) / den if den > 0 else 0)
        f0.append(sr / tau); conf.append(1 - cm[t])
    return np.array(f0), np.array(conf), hop

def midi(f): return 69 + 12 * np.log2(f / 440.0)

SCALES = {1: [0, 2, 4, 5, 7, 9, 11], 2: [0, 2, 3, 5, 7, 8, 10]}
NAMES = ["Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si"]

def key_of(m):
    m = m[~np.isnan(m)]
    pcs = np.round(m) % 12
    h = np.array([np.sum(pcs == k) for k in range(12)], dtype=float)
    best = None
    for sc, iv in SCALES.items():
        for k in range(12):
            s = sum(h[(k + i) % 12] for i in iv) + 0.3 * h[k]
            if best is None or s > best[0]: best = (s, k, sc)
    return best[1], best[2]

def vibrato_depth(m, hop, sr):
    # desviación do ton respecto da súa media móbil de 150 ms (o vibrato e as microvariacións)
    w = max(3, int(0.15 * sr / hop))
    out = []
    seg = []
    for v in list(m) + [np.nan]:
        if np.isnan(v):
            if len(seg) > 2 * w:
                s = np.array(seg); k = np.ones(w) / w
                sm = np.convolve(s, k, mode="same")
                out.extend(list((s - sm)[w:-w]))
            seg = []
        else: seg.append(v)
    return np.std(out) * 100 if out else 0.0

def main():
    if sys.argv[1] == "ton":
        sr, x = load(sys.argv[2])
        f0, _, _ = yin(x, sr)
        k, sc = key_of(midi(f0))
        print(k, sc)
        return
    sr, x = load(sys.argv[2]); _, y = load(sys.argv[3])
    fi, ci, hop = yin(x, sr); fo, co, _ = yin(y, sr)
    n = min(len(fi), len(fo)); fi, fo = fi[:n], fo[:n]
    mi, mo = midi(fi), midi(fo)
    ok = ~np.isnan(mi) & ~np.isnan(mo)
    di = np.abs(mi[ok] - np.round(mi[ok])) * 100
    do = np.abs(mo[ok] - np.round(mo[ok])) * 100
    def centre(mm):
        # centro de cada nota: media móbil de 250 ms dentro dos tramos con voz, só onde o ton está estable
        w = max(3, int(0.25 * sr / hop)); res = []
        seg = []; idx = []
        for i, v in enumerate(list(mm) + [np.nan]):
            if np.isnan(v):
                if len(seg) > w:
                    a = np.array(seg); sm = np.convolve(a, np.ones(w) / w, mode="valid")
                    rng = np.array([a[j:j + w].max() - a[j:j + w].min() for j in range(len(a) - w + 1)])
                    res.extend(list(np.abs(sm - np.round(sm))[rng < 1.2] * 100))
                seg = []
            else: seg.append(v)
        return np.mean(res) if res else 0.0
    print(f"tramas con voz {ok.sum():4d} | distancia ao semitono: entrada {di.mean():5.1f} c -> saída {do.mean():5.1f} c "
          f"(saída <10 c: {100*np.mean(do<10):4.0f} %) | vibrato entrada {vibrato_depth(mi,hop,sr):4.1f} c, saída {vibrato_depth(mo,hop,sr):4.1f} c "
          f"| centro das notas {centre(mi):4.1f} c -> {centre(mo):4.1f} c | perdidas {np.sum(~np.isnan(mi) & np.isnan(mo))}")
    if len(sys.argv) > 4:
        import matplotlib; matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        t = np.arange(n) * hop / sr
        fig, ax = plt.subplots(figsize=(12, 4), dpi=110)
        lo = np.nanmin(mi) - 1; hi = np.nanmax(mi) + 1
        for k in range(int(lo), int(hi) + 1):
            ax.axhline(k, color="#ddd", lw=0.6, zorder=0)
        ax.plot(t, mi, color="#e9a24a", lw=1.2, label="entrada")
        ax.plot(t, mo, color="#d8483c", lw=1.6, label="saída")
        ax.set_ylim(lo, hi); ax.set_xlabel("segundos"); ax.set_ylabel("nota MIDI")
        ax.set_title(sys.argv[5] if len(sys.argv) > 5 else "")
        ax.legend(loc="upper right"); fig.tight_layout(); fig.savefig(sys.argv[4])

main()
