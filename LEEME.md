**Barullo Tune** é un afinador de voz en tempo real. En **Directo** non ten latencia (menos de 1 ms), para cantar escoitándote; en **Estudio** mantén o timbre aínda transpondo unha oitava e deixa mover os formantes. Vai dende unha corrección natural, que respecta o vibrato, ata o efecto robot.

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

- **Pantalla**: a nota á que vai a voz, cun arco de −50 a +50 cents (agulla laranxa: a túa voz; branca: a afinada), os hercios e os cents que corrixe, os medidores de entrada e saída, e o ton dos últimos segundos.
- **Teclado**: cinco oitavas, cunha tira enriba de cada tecla que di se a nota está **na escala**, **fóra** ou **libre** (cantando nela non se corrixe). Clic para poñer ou quitar; clic dereito para deixala libre. O que queda fóra do rango da voz vese máis escuro; a nota á que vai a voz acéndese.
- **Tónica e escala** (cromática, maior, menores, pentatónicas, blues, dórica, mixolidia ou personalizada) e **Detectar**: canta uns segundos e escolle a tonalidade.
- **Voz**: baixo, barítono, tenor, alto, mezzo, soprano ou calquera. O afinador só busca no seu rango (sen saltos de oitava).
- **Modos**: escala, MIDI como escala (as notas que tocas fan de escala) e MIDI nota (a voz vai á nota que tocas).
- **Afinar**: **velocidade** (canto tarda en afinar dentro da nota) e **transición** (canto tarda en pasar dunha nota á seguinte), que se poden **ligar**.
- **Tolerancia**: desafinacións menores ca uns cents, ou máis curtas ca un tempo, non se tocan.
- **Voz**: **vibrato** de 0 a 200 % (o vibrato mantense aínda corrixindo rápido), **cantidade** de corrección e **formantes** (en Estudio).
- **Saída**: transpor ±12 semitonos, La de referencia (415–466 Hz), mestura coa voz orixinal (aliñada) e volume.
- **Efecto robot**: velocidade e transición a 0 e vibrato a 0 %.

## Como funciona

| Ficheiro | Que é |
|---|---|
| `Source/tune/Engine.h` | O motor. Detector de ton tipo YIN (sinal diezmada a ~12 kHz, cada 4 ms), con continuidade e confirmación ao comezar. Marcas de ciclo aliñadas por correlación, con precisión de fracción de mostra. **Directo**: a voz lese a outra velocidade e salta un ciclo enteiro cando se adianta ou se atrasa (fundido de 0,75 ms; latencia 0,9 ms); o ton mídese ciclo a ciclo e extrapólase ao punto que se le. **Estudio**: TD-PSOLA en tempo real, cada grano corrixido co seu propio período, e formantes lendo o grano a outra velocidade. Modelo de corrección común: o vibrato sepárase cun filtro de banda (3–8 Hz); o centro vai á nota con «velocidade», a nota de destino móvese con «transición» e o vibrato volve sumarse. |
| `Source/Plugin*.cpp` | O plugin JUCE e a interface |
| `tools/RenderTest.cpp` | Proba sen interface; con `--file entrada.wav saida.wav parámetro=valor…` procesa un ficheiro |
| `tools/analise.py` | Mide a afinación dunha voz antes e despois (YIN, vibrato, centro das notas, gráfico) |
| `tools/voces/LEEME.md` | De onde saen as voces reais das probas |
| `packaging/` | Instaladores para Mac, Windows e Linux |

## Probas

- **Voz sintética desafinada** (32 cents de media, con vibrato, glisandos e consoantes), efecto robot: 0,8 cents en Directo e 0,2 en Estudio. Transpor +5, MIDI e formantes caen na nota; vibrato ao 100 %: notas centradas (±0,3 c) e vibrato intacto; tolerancia e notas libres deixan a voz.
- **Voces reais** (vocadito, NIT, Essentia), efecto robot: o centro das notas pasa de 21–26 cents a 1–9 cents.
- **Transparencia**: coa corrección a 0 % a voz sae igual (mesmo nivel e periodicidade).
- **Bloques** de 37 e de 512 mostras, mono e estéreo: mesmo resultado. **Mestura 0 %** = voz orixinal atrasada exactamente a latencia.
- **Cambiar de xeito** en marcha: a latencia nova chega ao DAW. **Detectar tonalidade**: Do maior na melodía de proba.
- **Estado** gardado e recuperado. Silencio e ruído sen valores raros.
- **pluginval** ao nivel máximo (10), con probas de interface; auval no Mac.

## Publicar unha versión

```
git tag v0.1.0 && git push origin v0.1.0
```

A versión sae da liña `project(BarulloTune VERSION …)` de `CMakeLists.txt`.

## Créditos

- Detector de ton baseado en YIN (de Cheveigné e Kawahara, 2002). Desprazamento por TD-PSOLA (Moulines e Charpentier, 1990). Código propio.

## Licencia

Barullo Tune es software libre: se publica bajo la licencia **GNU Affero General Public License v3.0** (archivo `LICENSE`). Puedes usarlo, estudiarlo, modificarlo y compartirlo; si distribuyes una versión modificada, tiene que ir con su código y bajo la misma licencia.

Usa JUCE 8 bajo su licencia AGPLv3. La tipografía Barlow va con su licencia OFL (`Resources/Barlow-OFL.txt`).
