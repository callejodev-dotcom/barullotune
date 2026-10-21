#!/usr/bin/env python3
"""Compara as métricas dunha execución coa base e falla se algo empeora.

  compara.py base.json novas.json            -> táboa e código 1 se algo empeorou
  compara.py base.json novas.json --gardar   -> as novas pasan a ser a base

Regras segundo o nome da métrica:
  cpu.*        só informa (cada máquina mide distinto)
  voz.<nome>.* só informa (cada voz soa é ruidosa); mándan as medias voz.media.*
  fallos       ten que ser 0
  *latencia*   non pode cambiar (un cambio de latencia ten que ser a propósito: --gardar)
  *.fondo      vibrato que se respecta: ±3 c
  *.fora       tramas fóra de nota: máis peor, con marxe de 2 ou 20 %
  voz.media.*, detector.*, *.pct
               contan poucos sucesos e cambian algo dun procesador a outro (a base é dun
               Mac ARM; o CI tamén corre en x86 e con MSVC): marxe de 1 ou 25 %
  resto        canto máis baixo mellor (cents, dB): marxe de 0,5 ou 20 %
"""
import json, sys

def rule(k, b, n):
    """devolve (ok, texto)"""
    if k.startswith("cpu.") or (k.startswith("voz.") and not k.startswith("voz.media.")):
        return True, "info"
    if k == "fallos":
        return n == 0, ""
    if "latencia" in k:
        return abs(n - b) < 0.05, "igual"
    if k.endswith(".fondo"):
        return abs(n - b) <= 3.0, "±3"
    if k.endswith(".fora"):
        return n <= b + max(2.0, 0.2 * b), ""
    if k.startswith("voz.media.") or k.startswith("detector.") or k.endswith(".pct"):
        return n <= b + max(1.0, 0.25 * b), ""
    return n <= b + max(0.5, 0.2 * b), ""

def main():
    base = json.load(open(sys.argv[1]))
    new = json.load(open(sys.argv[2]))
    if "--gardar" in sys.argv:
        json.dump(new, open(sys.argv[1], "w"), indent=2, sort_keys=True)
        print(f"base actualizada: {sys.argv[1]}")
        return 0
    bad = 0
    print(f"{'métrica':44s} {'base':>9s} {'agora':>9s}")
    for k in sorted(set(base) | set(new)):
        if k not in new:
            print(f"{k:44s} {base[k]:9.2f} {'—':>9s}  FALTA"); bad += 1; continue
        if k not in base:
            print(f"{k:44s} {'—':>9s} {new[k]:9.2f}  nova"); continue
        b, n = base[k], new[k]
        ok, note = rule(k, b, n)
        mark = "" if ok else "  PEOR"
        if ok and note != "info" and n < b - max(0.5, 0.2 * b) and not k.endswith(".fondo") and "latencia" not in k:
            mark = "  mellor"
        print(f"{k:44s} {b:9.2f} {n:9.2f}{mark}{('  (' + note + ')') if note and note != 'igual' else ''}")
        bad += 0 if ok else 1
    print("sen retrocesos" if bad == 0 else f"{bad} métricas empeoraron")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
