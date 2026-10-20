Voces reais para probar o motor. Non van no repo, cada unha ten a súa licenza; descárganse así:

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/spotify/basic-pitch && (cd basic-pitch && git sparse-checkout set tests/resources)
#   tests/resources/vocadito_10.wav, vocadito_14.wav: dataset vocadito (Bittner et al.), CC BY 4.0
git clone --depth 1 --filter=blob:none --no-checkout https://github.com/nnsvs/nnsvs && (cd nnsvs && git checkout HEAD -- tests/data)
#   nitech_jp_song070_f001_*.wav: NIT Japanese song database (HTS working group)
git clone --depth 1 --filter=blob:none --no-checkout https://github.com/MTG/essentia && (cd essentia && git checkout HEAD -- src/examples/python/musicbricks-tutorials)
#   singing-female.wav, flamenco.wav: exemplos de Essentia
```

Procesar e medir:

```
build/BTRenderTest_artefacts/Release/BTRenderTest --file voz.wav robot.wav tonica=9 escala=1 velocidade=0
python3 tools/analise.py ton voz.wav                     # tonalidade estimada: tónica e escala
python3 tools/analise.py comp voz.wav robot.wav graf.png # desafinación, vibrato e gráfico
```
