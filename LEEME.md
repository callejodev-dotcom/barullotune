**Barullo Tune** é un afinador de voz en tempo real con tres xeitos: **Directo** (menos de 1 ms de latencia, para cantar escoitándote), **Preciso** (12 ms, afina o dobre de exacto) e **Estudio** (mantén o timbre aínda transpondo unha oitava e deixa mover os formantes). Vai dende unha corrección natural, que respecta o vibrato, ata o efecto robot.

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
- **Efecto robot**: velocidade e transición a 0 e vibrato a 0 % (ou o preset «Robot»).
- **Presets**: Natural, Suave, Pop, Balada, Robot, Trap, Directo seguro, Xigante e Trasno. Só cambian o carácter da corrección, non a tonalidade nin a voz; tamén aparecen no menú de presets do DAW.
- **Comparar**: escoita a voz sen afinar, aliñada no tempo.
- **Estéreo de verdade**: o ton detéctase no centro e as dúas canles corríxense igual (a imaxe estéreo non se move).
- **Xanela** de 75 % a 150 % (arrastrando a esquina); o tamaño gárdase co proxecto.

## Como funciona

| Ficheiro | Que é |
|---|---|
| `Source/tune/Engine.h` | O motor. Detector de ton tipo YIN (sinal diezmada a ~12 kHz, cada 4 ms), con continuidade e confirmación ao comezar. Marcas de ciclo aliñadas por correlación, con precisión de fracción de mostra. Lectura entre mostras con sinc de 16 puntos (agudos planos ata 16 kHz). **Directo** e **Preciso**: a voz lese a outra velocidade e salta un ciclo enteiro cando se adianta ou se atrasa (fundido de 0,6 ms; latencia 0,9 ms); o ton mídese ciclo a ciclo; en Directo extrapólase ao punto que se le e en Preciso (a lectura vai 11,5 ms detrás) interpólase entre ciclos xa medidos. **Estudio**: TD-PSOLA en tempo real, cada grano corrixido co seu propio período, e formantes lendo o grano a outra velocidade. Modelo de corrección común: o vibrato sepárase cun filtro de banda (3–8 Hz); o centro vai á nota con «velocidade», a nota de destino móvese con «transición» e o vibrato volve sumarse. |
| `Source/Plugin*.cpp` | O plugin JUCE e a interface |
| `tools/RenderTest.cpp` | Proba sen interface; con `--file entrada.wav saida.wav parámetro=valor…` procesa un ficheiro |
| `tools/analise.py` | Mide a afinación dunha voz antes e despois (YIN, vibrato, centro das notas, gráfico) |
| `tools/voces/LEEME.md` | De onde saen as voces reais das probas |
| `packaging/` | Instaladores para Mac, Windows e Linux |

## Probas

- **Voz sintética desafinada** (32 cents de media, con vibrato, glisandos e consoantes), efecto robot: 0,8 cents en Directo e 0,2 en Estudio. Transpor +5, MIDI e formantes caen na nota; vibrato ao 100 %: notas centradas (±0,3 c) e vibrato intacto; tolerancia e notas libres deixan a voz.
- **Voces reais** (vocadito, NIT, Essentia), efecto robot, distancia media á nota (entrada 11–26 c): Directo 2–11 c, Preciso 1,5–4,4 c, Estudio 0,8–3,1 c.
- **Condicións reais**: voz moi baixa (picos a −45 dBFS), ruído rosa a 20 dB de SNR, reverberación e 96 kHz: afina igual ca coa voz limpa (con ruído a 10 dB, algo menos).
- **CPU**: arredor do 1,5 % dun núcleo por instancia (estéreo, 48 kHz) en calquera xeito.
- **Transparencia**: coa corrección a 0 % a voz sae igual (mesmo nivel e periodicidade). Resposta en agudos: plana ata 16 kHz (antes, coa interpolación lineal, −2,9 dB a 12–16 kHz).
- **Estéreo**: L e R corríxense igual (R = 0,6·L atrasada 3 ms segue sendo 0,6·L). **Comparar** = voz orixinal aliñada. **Presets** aplícanse e gárdanse.
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
