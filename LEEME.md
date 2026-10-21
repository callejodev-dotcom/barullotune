**Barullo Tune** é un afinador de voz en tempo real con tres xeitos: **Directo** (menos de 1 ms de latencia, para cantar escoitándote), **Preciso** (12 ms, afina o dobre de exacto) e **Estudio** (o máis exacto, copiando ciclos enteiros). En todos, transpor non cambia o timbre (sen voz de esquío) e os formantes pódense mover á parte. Vai dende unha corrección natural, que respecta o vibrato, ata o efecto robot.

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
- **Modos**: escala, MIDI como escala (as notas que tocas fan de escala) e MIDI nota (a voz vai á nota que tocas; a roda de pitch bend dóbraa ±2 semitonos).
- **Afinar**: **velocidade** (canto tarda en afinar dentro da nota) e **transición** (canto tarda en pasar dunha nota á seguinte), que se poden **ligar**.
- **Tolerancia**: desafinacións menores ca uns cents, ou máis curtas ca un tempo, non se tocan.
- **Voz**: **vibrato** de 0 a 200 % (o vibrato mantense aínda corrixindo rápido), **cantidade** de corrección e **formantes**.
- **Saída**: transpor ±12 semitonos, La de referencia (415–466 Hz), mestura coa voz orixinal (aliñada) e volume.
- **Efecto robot**: velocidade e transición a 0 e vibrato a 0 % (ou o preset «Robot»).
- **Presets**: Natural, Suave, Pop, Balada, Robot, Trap, Directo seguro, Xigante e Trasno. Só cambian o carácter da corrección, non a tonalidade nin a voz; tamén aparecen no menú de presets do DAW.
- **Comparar**: escoita a voz sen afinar, aliñada no tempo.
- **Estéreo de verdade**: o ton detéctase no centro e as dúas canles corríxense igual (a imaxe estéreo non se move).
- **Xanela** de 75 % a 150 % (arrastrando a esquina); o tamaño gárdase co proxecto.
- **Bypass** de verdade: a voz orixinal sae atrasada a mesma latencia ca o efecto (o DAW non a despraza) e cun fundido de 20 ms, sen clic. O motor segue escoitando, así ao volver xa sabe por onde vai a voz.
- **Proxectos vellos**: o estado leva número de versión; ao abrir un proxecto dunha versión anterior, os axustes tradúcense e os parámetros novos chegan co seu valor de fábrica.

## Como funciona

| Ficheiro | Que é |
|---|---|
| `Source/tune/` | O motor, en pezas: `Analysis.h` (detector, marcas de ciclo, ton deslizante e decisión da nota, común a todas as voces), `DirectVoice.h` (Directo e Preciso), `StudioVoice.h` (Estudio, PSOLA), `Correction.h` (axustes e modelo de corrección), `FormantFix.h`, `PitchDetector.h`, `Dsp.h` e `Engine.h`, que as xunta. Como funciona: Detector de ton tipo YIN (sinal diezmada a ~12 kHz, cada 4 ms), con continuidade e confirmación ao comezar. Marcas de ciclo aliñadas por correlación, con precisión de fracción de mostra. Lectura entre mostras con sinc de 16 puntos (agudos planos ata 16 kHz). **Directo** e **Preciso**: a voz lese a outra velocidade e salta un ciclo enteiro cando se adianta ou se atrasa (fundido de 0,6 ms; latencia 0,9 ms); o ton mídese cada poucas mostras nunha xanela deslizante de período e medio que acaba na última mostra (correlación normalizada arredor do período anterior, con parábola): as medidas solápanse, así non teñen o tremor dun ciclo solto, e o ton no punto que se le interpólase entre elas (en Directo, extrapólase un anaco coa pendente do último período; en Preciso, a lectura vai 11,5 ms detrás e sempre cae entre medidas). O ton mírase 1,5 ms por diante do punto de lectura, o mesmo que tarda o suavizado da corrección, para que a corrección chegue a tempo. Ao transpor ou mover os formantes, unha **corrección de formantes sen latencia** mide cada 5 ms a envolvente (predición lineal de orde ~37) da voz orixinal e da lida, e coa lida fai: branquear coa súa envolvente e colorear coa orixinal (celosías con coeficientes de reflexión interpolados, sempre estables). **Estudio**: TD-PSOLA en tempo real, cada grano corrixido co seu propio período, e formantes lendo o grano a outra velocidade. Modelo de corrección común: o vibrato sepárase cun filtro de banda (3–8 Hz) e só se respecta se é regular (medios ciclos de 60–140 ms parecidos entre si e máis de 8 cents): a oscilación irregular dunha voz insegura corríxese coma calquera desafinación; o centro vai á nota con «velocidade», a nota de destino móvese con «transición» e o vibrato volve sumarse. Nos portamentos (a voz movéndose máis de 4 semitonos/s) a corrección sóltase en proporción á transición, para non arrastrar a voz cara á nota anterior; e a nota nova acéptase xa se a voz vai claramente cara a ela. |
| `Source/Plugin*.cpp` | O plugin JUCE e a interface |
| `tools/RenderTest.cpp` | Proba sen interface; con `--file entrada.wav saida.wav parámetro=valor…` procesa un ficheiro; con `--json metricas.json` garda as métricas; imprime a **huella da saída** (para comprobar que un cambio de organización non cambia nin un bit) |
| `tools/DetectorTest.cpp` | O detector en condicións difíciles, con ton verdadeiro coñecido (só o motor, sen JUCE): segundo harmónico dominante, sen fundamental, voz aireada, ruído a 10 e 5 dB, reverberación, voz grave, soprano e vibrato ancho |
| `tools/compara.py` | Compara as métricas coa base (`tools/metricas-base.json`, `tools/banco-base.json`) e falla se algo empeora; con `--gardar` actualiza a base |
| `tools/banco.py` | Banco de voces reais: cada voz polos tres xeitos, con cents, saltos (a saída sepárase máis de 1,6 semitonos da entrada) e perdas |
| `tools/analise.py` | Mide a afinación dunha voz antes e despois (YIN, vibrato, centro das notas, gráfico) |
| `tools/voces/LEEME.md` | De onde saen as voces reais das probas |
| `packaging/` | Instaladores para Mac, Windows e Linux |

## Probas

- **Voz sintética desafinada** (32 cents de media, con vibrato, glisandos e consoantes), efecto robot: 0,8 cents en Directo e 0,2 en Estudio. Transpor +5, MIDI e formantes caen na nota; vibrato ao 100 %: notas centradas (±0,3 c) e vibrato intacto; tolerancia e notas libres deixan a voz.
- **Vibrato**: nota longa 25 c baixa con vibrato regular de ±30 c: con vibrato 100 % sae centrada (±0,3 c) e co vibrato enteiro (±28 c); con 0 %, plana. Unha oscilación irregular de 25 c redúcese a ~11 c co preset Natural.
- **Voces reais** (vocadito, NIT, Essentia, flamenco; `tools/banco.py`), efecto robot e escala cromática, media das sete voces (entrada 11–26 c):

  | | Directo | Preciso | Estudio |
  |---|---|---|---|
  | afinación nas notas (cents) | 1,2 | 0,7 | 0,5 |
  | saltos (a saída a máis de 1,6 semitonos da voz) | 1,3 % (antes 6,2 %) | 1,1 % (antes 4,3 %) | 1,9 % (antes 7,8 %) |

  Os saltos eran a nota de destino quedando atrás nos glisandos rápidos. Agora cada voz de saída decide a súa nota co ton do punto que está lendo (Estudio, coa media dos ciclos a ±10 ms do grano, que xa ten medidos) e, nun glisando de máis dun semitono que non sexa vibrato, o suavizado da decisión volve empezar no ton de agora.
- **Detector en condicións difíciles** (`BTDetectorTest`): tramas con voz a máis de medio semitono do ton verdadeiro.

  | caso | antes | agora |
  |---|---|---|
  | voz limpa | 1,2 % | 1,2 % |
  | segundo harmónico dominante | 21,4 % | 2,0 % |
  | sen fundamental | 0,9 % | 1,0 % |
  | voz aireada (HNR ~5 dB) | 59,2 % | 13,9 % |
  | ruído rosa a 10 dB | 11,1 % | 0,5 % |
  | ruído rosa a 5 dB | 56,4 % | 4,7 % |
  | reverberación longa (RT60 1,2 s) | 33,5 % | 25,4 % |
  | voz grave, soprano, vibrato ancho | 0,5–1,2 % | 0,5–1,2 % |

  A oitava decídese mirando a familia do val elixido (a metade, o dobre e os múltiplos ata 4, e con ruído os submúltiplos ata 1/5): gaña o período máis curto que sexa case tan fondo coma o máis fondo, co fondo medido no atraso fraccionario (Lagrange cúbica), que nas voces agudas cae entre dúas mostras. Seguindo unha nota, a oitava só cambia con probas claras, para non facer saltos soltos nas voces graves reais.
- **Glisandos** sintéticos (ata 40 st/s), robot: tramas con a saída a máis de 1,1 semitonos da voz: 2,6–2,9 % (antes 16–23 %).
- **Vibrato rápido con tremor** (6,2 Hz, ±45 c, tremor de período de 0,6 % ciclo a ciclo), robot: a nota queda plana a 2,4 c en Directo (antes 6,0 c), 0,6 c en Preciso e 0,1 c en Estudio.
- **Condicións reais**: voz moi baixa (picos a −45 dBFS), ruído rosa a 20 dB de SNR, reverberación e 96 kHz: afina igual ca coa voz limpa (con ruído a 10 dB, algo menos).
- **Consoantes xordas ao transpor ±7** (unha «s» entre dúas vogais): o nivel da «s» cambia como moito 2,6 dB e sen estalos (antes, en Directo baixando, ata 7,8 dB e un estalo de +19 dB). En Directo e Preciso, sen ton a voz lese a velocidade 1 e a corrección de formantes apágase (non hai nada que corrixir), a xanela da orixinal vai aliñada co punto de lectura e un limitador impide que a corrección deixe a voz máis do dobre de forte.
- **Timbre ao transpor +5** (espectro medio en terzos de oitava, distancia ao orixinal): voz acelerada sen corrixir («esquío») 3,2 dB, Directo 1,7 dB, Estudio 1,4 dB. Nunha voz real: 12,9 dB sen corrixir, 3,5 dB en Directo, 3,2 dB en Estudio.
- **CPU** por instancia (estéreo, 48 kHz): arredor do 3 % dun núcleo en Directo e Preciso, 2 % en Estudio e 4,5 % en Directo mentres transpón (corrección de formantes).
- **Transparencia**: coa corrección a 0 % a voz sae igual (mesmo nivel e periodicidade). Resposta en agudos: plana ata 16 kHz (antes, coa interpolación lineal, −2,9 dB a 12–16 kHz).
- **Estéreo**: L e R corríxense igual (R = 0,6·L atrasada 3 ms segue sendo 0,6·L). **Comparar** = voz orixinal aliñada. **Presets** aplícanse e gárdanse.
- **Bloques** de 37 e de 512 mostras, mono e estéreo: mesmo resultado. **Mestura 0 %** = voz orixinal atrasada exactamente a latencia.
- **Cambiar de xeito** en marcha: a latencia nova chega ao DAW. **Detectar tonalidade**: Do maior na melodía de proba.
- **Estado** gardado e recuperado. Silencio e ruído sen valores raros.
- **Bypass** (o parámetro e o do DAW) = voz orixinal atrasada exactamente a latencia, nos tres xeitos.
- **Estado de 0.1** (sen versión): migra, o bypass vai ao valor de fábrica e o preset atópase polo nome. Estados rotos ou truncados non fan nada raro.
- **Automatización ao chou**: 60 s con todos os parámetros movéndose, bloques de 1 a 1024 mostras, MIDI, cambios de xeito e de frecuencia de mostraxe (22–192 kHz), con voz, ruído, silencio, continua, cadrada a 0 dBFS, impulsos e voz saturada: sempre finito e sen disparar.
- **Sanitizadores** (AddressSanitizer e UBSan) sobre toda a proba no CI; avisos como erros no código propio no Mac.
- **Sen retrocesos**: o CI compara as métricas (sintéticas nos tres sistemas e voces reais en Linux) coa base e falla se algo empeora.
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
