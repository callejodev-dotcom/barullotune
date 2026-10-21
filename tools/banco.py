#!/usr/bin/env python3
"""Banco de voces reais: pasa cada voz polos tres xeitos (efecto robot, escala cromática)
e mide a afinación da saída.

  banco.py <BTRenderTest> <carpeta coas voces> [--json saida.json]

As voces descárganse como di tools/voces/LEEME.md (calquera subcarpeta vale: búscanse polo nome).
Métricas por voz e xeito:
  cents   distancia media da saída ao semitono máis próximo (tramas con voz nas dúas)
  saltos  % de tramas nas que a saída se separa da entrada máis de 1,6 semitonos
          (un salto de oitava ou de nota que a corrección non debería facer)
  perdas  % de tramas con voz na entrada e sen ton claro na saída
"""
import json, os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analise

VOCES = ["vocadito_10", "vocadito_14", "nitech_jp_song070_f001_004", "nitech_jp_song070_f001_007",
         "nitech_jp_song070_f001_010", "flamenco", "singing-female"]
XEITOS = ["directo", "preciso", "estudio"]

def find(root, name):
    for d, _, files in os.walk(root):
        if name + ".wav" in files:
            return os.path.join(d, name + ".wav")
    return None

def measure(src, out):
    sr, x = analise.load(src); _, y = analise.load(out)
    fi, _, hop = analise.yin(x, sr); fo, _, _ = analise.yin(y, sr)
    n = min(len(fi), len(fo)); fi, fo = fi[:n], fo[:n]
    mi, mo = analise.midi(fi), analise.midi(fo)
    vi = ~np.isnan(mi); ok = vi & ~np.isnan(mo)
    d = np.abs(mo[ok] - np.round(mo[ok])) * 100
    jumps = np.abs(mo[ok] - mi[ok]) > 1.6
    return {"cents": float(d[~jumps].mean()) if (~jumps).any() else 0.0,
            "entrada": float((np.abs(mi[ok] - np.round(mi[ok])) * 100).mean()),
            "saltos": float(100 * jumps.mean()) if ok.any() else 0.0,
            "perdas": float(100 * np.mean(np.isnan(mo[vi]))) if vi.any() else 0.0}

def main():
    exe, root = sys.argv[1], sys.argv[2]
    js = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    res = {}
    tmp = tempfile.mkdtemp()
    print(f"{'voz':28s} {'entrada':>8s}" + "".join(f" {x:>22s}" for x in XEITOS))
    print(f"{'':28s} {'':8s}" + "".join(f" {'cents  saltos  perdas':>22s}" for _ in XEITOS))
    for v in VOCES:
        f = find(root, v)
        if f is None:
            print(f"{v:28s} (non está)"); continue
        row = f"{v:28s}"
        for q, name in enumerate(XEITOS):
            out = os.path.join(tmp, f"{v}_{name}.wav")
            subprocess.run([exe, "--file", f, out, f"calidade={q}", "escala=0", "velocidade=0", "transicion=0", "vibrato=0"],
                           check=True, stdout=subprocess.DEVNULL)
            m = measure(f, out)
            if q == 0: row += f" {m['entrada']:7.1f}c"
            row += f" {m['cents']:7.1f} {m['saltos']:6.2f}% {m['perdas']:5.1f}%"
            for k in ("cents", "saltos", "perdas"):
                res[f"voz.{v}.{name}.{k}"] = round(m[k], 3)
        print(row)
    for name in XEITOS:
        for k in ("cents", "saltos", "perdas"):
            vals = [res[f"voz.{v}.{name}.{k}"] for v in VOCES if f"voz.{v}.{name}.{k}" in res]
            if vals: res[f"voz.media.{name}.{k}"] = round(float(np.mean(vals)), 3)
    print("media" + " " * 32 + "".join(f" {res.get(f'voz.media.{n}.cents', 0):7.1f} {res.get(f'voz.media.{n}.saltos', 0):6.2f}% {res.get(f'voz.media.{n}.perdas', 0):5.1f}%" for n in XEITOS))
    if js:
        json.dump(res, open(js, "w"), indent=2, sort_keys=True)

if __name__ == "__main__":
    main()
