#!/bin/bash
# Fai as capturas da interface para o manual (docs/img), desde a proba sen interface.
# Uso: tools/capturas.sh [BTRenderTest]   (por defecto, o de build-dev)
set -eo pipefail
cd "$(dirname "$0")/.."
BIN="${1:-build-dev/BTRenderTest_artefacts/Release/BTRenderTest}"
[ -x "$BIN" ] || { echo "Non atopo $BIN (compila con -DBARULLO_TESTS=ON)"; exit 1; }
mkdir -p docs/img
names=(correccion voz armonias saida informe)
for i in 0 1 2 3 4; do
  BT_LANG=0 "$BIN" --snapshot "docs/img/${names[$i]}.png" "$i" > /dev/null
  echo "docs/img/${names[$i]}.png"
done
