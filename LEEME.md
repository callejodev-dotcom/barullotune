**Barullo Tune** é un corrector de afinación para voz: dende o efecto robot ata unha corrección natural que respecta o vibrato. Funciona en directo, con 34 ms de latencia, e non cambia os formantes, polo que a voz non soa a esquío ao movela.

Unha ferramenta de [Escoita](https://www.escoita.link).

## Descargas

| Sistema | Instalador | Á man |
|---|---|---|
| **Mac** (Apple Silicon e Intel, macOS 11+): AU, VST3 e app | `BarulloTune-…-mac.pkg` | `BarulloTune-…-mac.zip` |
| **Windows** (64 bits): VST3 e app | `BarulloTune-…-windows-setup.exe` | `BarulloTune-…-windows-x64.zip` |
| **Linux** (x86_64): VST3, LV2 e app | — | `BarulloTune-…-linux-x86_64.tar.gz` → `./instalar.sh` |

Se o sistema avisa ao abrilo:
- **Mac:** clic dereito sobre o instalador → *Abrir* → *Abrir*.
- **Windows:** «Windows protexeu o teu PC» → *Máis información* → *Executar de todos os xeitos*.

Avisan porque os instaladores non levan sinatura de pago, non porque haxa nada raro.

## Que ten

- **Escala**: tónica e escala (cromática, maior, menor natural e harmónica, pentatónicas, blues, dórica, mixolidia) ou as notas que acendas a man nas teclas.
- **Modos**:
  - **Escala**: vai á nota permitida máis próxima.
  - **MIDI: escala**: as notas que tocas no teclado fan de escala.
  - **MIDI: nota**: a voz vai á nota exacta que tocas, na oitava máis próxima.
- **Velocidade**: ao mínimo, efecto robot (salta de nota en seco). Entre 20 e 80 ms, corrección clara. Por riba, natural.
- **Cantidade**, **humanizar** (as notas longas relaxan a corrección), **transpor** ±12 semitonos, **La** de referencia, **mestura** coa voz orixinal (aliñada no tempo) e **volume**.
- **Pantalla**: a nota á que vai a voz, os hercios e os cents que corrixe, un medidor de −50 a +50 cents e o gráfico do ton da entrada e da saída.

## Como funciona

| Ficheiro | Que é |
|---|---|
| `Source/tune/Engine.h` | O motor: detector de ton tipo YIN sobre a sinal diezmada a ~12 kHz, cada 4 ms, con continuidade (non salta de oitava no medio dunha nota) e confirmación ao comezar. TD-PSOLA en tempo real: marcas de ciclo aliñadas por correlación con precisión de fracción de mostra, e cada grano corrixido co seu propio período. |
| `Source/Plugin*.cpp` | O plugin JUCE e a interface |
| `tools/RenderTest.cpp` | Proba sen interface; con `--file entrada.wav saida.wav parámetro=valor…` procesa un ficheiro |
| `tools/analise.py` | Mide a afinación dunha voz antes e despois (YIN, vibrato, centro das notas, gráfico) |
| `tools/voces/LEEME.md` | De onde saen as voces reais das probas |
| `packaging/` | Instaladores para Mac, Windows e Linux |

## Probas

- **Voz sintética desafinada** (unha media de 32 cents, con vibrato, glisandos e consoantes): en modo robot, a saída queda a 0,0 cents de media. Transpor +5 e o modo MIDI caen na nota.
- **Voces reais** (vocadito, NIT, Essentia), en modo robot: a distancia á nota pasa de 22–27 cents a 1–4 cents.
- **Transparencia**: coa corrección a 0 %, a voz sae practicamente igual (periodicidade 0,951 → 0,947).
- **Bloques** de 37 e de 512 mostras, mono e estéreo: mesmo resultado.
- **Mestura 0 %** = voz orixinal atrasada exactamente a latencia.
- **Estado** gardado e recuperado. Silencio e ruído sen valores raros.
- **pluginval** ao nivel máximo (10), con probas de interface; auval no Mac.

## Publicar unha versión

```
git tag v0.1.0 && git push origin v0.1.0
```

A versión sae da liña `project(BarulloTune VERSION …)` de `CMakeLists.txt`.

## Créditos e licenzas

- Detector de ton baseado en YIN (de Cheveigné e Kawahara, 2002). Desprazamento por TD-PSOLA (Moulines e Charpentier, 1990). Código propio.
- Tipografía Barlow (licenza OFL, `Resources/Barlow-OFL.txt`).
