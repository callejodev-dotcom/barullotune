#!/bin/bash
# As probas de Barullo Tune na CI común (escoita-nucleo): chámase desde a raíz con
# mac, windows, linux ou sanitizadores, coa compilación en build/
set -eo pipefail
SO="$1"
EXE=""; [ "$SO" = windows ] && EXE=".exe"
PY=python3; command -v python3 > /dev/null || PY=python
CONF=Release; [ "$SO" = sanitizadores ] && CONF=RelWithDebInfo
RT="build/BTRenderTest_artefacts/$CONF/BTRenderTest$EXE"
DT="build/BTDetectorTest_artefacts/$CONF/BTDetectorTest$EXE"

if [ "$SO" = sanitizadores ]; then
  "$RT"
  "$DT"
  exit 0
fi

echo "::group::Proba de son (afinación, bloques e estado)"
"$RT" prueba.wav --json metricas.json
echo "::endgroup::"
echo "::group::Métricas (sen retrocesos respecto da base)"
$PY tools/compara.py tools/metricas-base.json metricas.json
echo "::endgroup::"
echo "::group::Detector en condicións difíciles"
"$DT" --json detector.json
$PY tools/compara.py tools/detector-base.json detector.json
echo "::endgroup::"

echo "::group::Coma nun DAW: o VST3 de verdade, coa edición (capturar, editar, volver tocar, abrir o proxecto, render, Estudio)"
HT="build/BTHostTest_artefacts/$CONF/BTHostTest$EXE"
VST3="build/BarulloTune_artefacts/$CONF/VST3/Barullo Tune.vst3"
if [ "$SO" = linux ] && command -v xvfb-run > /dev/null; then xvfb-run -a "$HT" "$VST3"; else "$HT" "$VST3"; fi
echo "::endgroup::"

if [ "$SO" = linux ]; then
  echo "::group::Voces reais (sen retrocesos respecto da base)"
  V="${RUNNER_TEMP:-/tmp}/voces"
  mkdir -p "$V"
  ( cd "$V"
    git clone -q --depth 1 --filter=blob:none --sparse https://github.com/spotify/basic-pitch && (cd basic-pitch && git sparse-checkout set tests/resources)
    git clone -q --depth 1 --filter=blob:none --no-checkout https://github.com/nnsvs/nnsvs && (cd nnsvs && git checkout -q HEAD -- tests/data)
    git clone -q --depth 1 --filter=blob:none --no-checkout https://github.com/MTG/essentia && (cd essentia && git checkout -q HEAD -- src/examples/python/musicbricks-tutorials) )
  $PY -m pip install -q numpy scipy
  $PY tools/banco.py "$RT" "$V" --json banco.json
  $PY tools/compara.py tools/banco-base.json banco.json
  echo "::endgroup::"
fi
