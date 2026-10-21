#!/usr/bin/env python3
"""Comproba as traducións: cada texto do código dentro de T("…") ou U("…") (e, se se
pasa, os que se pediron mentres corrían as probas) ten que estar en Source/lang/Strings.h
cos seis idiomas.

  idiomas.py                      -> lista o que falta; código 1 se falta algo
  idiomas.py --claves claves.txt  -> tamén as claves apuntadas con BT_LANG_KEYS=claves.txt
"""
import re, sys, os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SOURCES = ["Source/PluginProcessor.cpp", "Source/PluginEditor.cpp", "Source/PluginProcessor.h"]
LANGS = ["gl", "es", "en", "pt", "ca", "eu"]

def unescape(s):
    return s.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")

def code_keys():
    keys = set()
    for p in SOURCES:
        s = open(os.path.join(ROOT, p), encoding="utf-8").read()
        for m in re.finditer(r'\b[TU] \("((?:[^"\\]|\\.)*)"\)', s):
            keys.add(unescape(m.group(1)))
        # listas de textos que pasan por U() máis abaixo (nomes e consellos)
        for m in re.finditer(r'const char\*\s*\w+\[[^\]]*\]\s*=\s*\{(.*?)\};', s, re.S):
            for q in re.finditer(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
                keys.add(unescape(q.group(1)))
        # os nomes e consellos dos mandos (setupKnob (k, "id", "Nome", "consello"))
        for m in re.finditer(r'setupKnob \(\w+, "[^"]+", (?:U \()?"((?:[^"\\]|\\.)*)"\)?(?:\.toRawUTF8\(\))?,\s*"((?:[^"\\]|\\.)*)"\)', s, re.S):
            keys.add(unescape(m.group(1))); keys.add(unescape(m.group(2)))
    # escalas e presets (nomes na táboa)
    h = open(os.path.join(ROOT, "Source/PluginProcessor.h"), encoding="utf-8").read()
    for m in re.finditer(r'\{ "([^"]+)",\s+0b?x?[0-9A-Fa-f]', h):
        keys.add(m.group(1))
    c = open(os.path.join(ROOT, "Source/PluginProcessor.cpp"), encoding="utf-8").read()
    for m in re.finditer(r'\{ "([^"]+)",\s+"', c):   # presets
        keys.add(m.group(1))
    return {k for k in keys if re.search(r"[A-Za-zÁÉÍÓÚáéíóúñ]", k) and not k.startswith("_") and k not in ("MTS-ESP", "MIDI", "Barullo Tune", ".scl", "A", "B", "C")}

def table():
    s = open(os.path.join(ROOT, "Source/lang/Strings.h"), encoding="utf-8").read()
    rows = {}
    for m in re.finditer(r'\{ \{ ((?:"(?:[^"\\]|\\.)*"\s*,?\s*)+)\} \}', s):
        cells = [unescape(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))]
        rows[cells[0]] = cells
    return rows

def main():
    keys = code_keys()
    if "--claves" in sys.argv:
        f = sys.argv[sys.argv.index("--claves") + 1]
        for ln in open(f, encoding="utf-8"):
            ln = ln.rstrip("\n").replace("\\n", "\n")
            if re.search(r"[A-Za-zÁÉÍÓÚáéíóúñ]", ln) and ln not in ("MTS-ESP", "MIDI", "Barullo Tune", ".scl"): keys.add(ln)
    rows = table()
    missing = sorted(k for k in keys if k not in rows)
    incomplete = sorted(k for k, r in rows.items() if len(r) != len(LANGS) or any(not x for x in r))
    for k in missing: print("falta:", k)
    for k in incomplete: print("incompleta:", k)
    print(f"{len(keys)} textos, {len(rows)} na táboa, {len(missing)} faltan, {len(incomplete)} incompletos")
    return 1 if missing or incomplete else 0

if __name__ == "__main__":
    sys.exit(main())
