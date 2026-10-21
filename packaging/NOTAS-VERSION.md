**Barullo Tune** é un corrector de afinación para voz: dende o efecto robot ata unha corrección natural que respecta o vibrato, con escalas, notas por MIDI e sen cambiar os formantes da voz.

Unha ferramenta de [Escoita](https://www.escoita.link). Manual: [docs/MANUAL.md](https://github.com/callejodev-dotcom/barullotune/blob/main/docs/MANUAL.md).

## Novidades na 0.2.1

- **Directo sen latencia para o DAW**: declara 0 mostras (o atraso que queda é de 0,25 ms) e xa non atrasa as outras pistas.
- **Son máis limpo en Directo e Preciso**: en cada salto de ciclo perdíase unha mostra. Agora non hai ruído entre os harmónicos ao corrixir (de −20/−40 dB a −50/−78 dB) e as notas caen máis xustas: armonías, MIDI nota e transpor quedan a menos de 0,4 cents (antes 1,2–3,5).
- **Reverberación e legato**: ao cambiar de nota, a cola da anterior xa non leva o detector dúas oitavas abaixo (erros 20 → 6,6 %).
- **Voz aireada e con ruído**: unha «i» aguda e chea de aire xa non queda sen afinar (erros 8,2 → 2,6 %; tramas perdidas con ruído a 5 dB, 10,8 → 5,5 %).
- Na versión publicada da 0.2.0 faltaba o .zip do Mac; nesta vai.

## Novidades na 0.2.0

- **Armonías**: ata catro voces novas a partir da túa (intervalos da escala ou acorde MIDI), cada unha co seu nivel, panorama, desafinación, atraso e formantes.
- **Vibrato xerado** (fondo, velocidade, retardo, variación e trémolo), **flexibilidade** (as notas que buscas a propósito quedan), **humanizar** (as notas longas respiran) e **aire**.
- **Escalas e afinación**: vinte escalas (tamén a frixia dominante do flamenco), afinación xusta ou personalizada nota a nota, escalas de **Scala** (.scl) e **MTS-ESP**.
- **MIDI**: as notas que cantas saen como notas MIDI, **MPE** e rango do pitch bend.
- **Detectar a tonalidade** tamén coa base, pola entrada lateral.
- **Presets propios**, **A/B**, **desfacer**, bypass sen desprazar o son e formato **CLAP**.
- **Interface nova** por pestanas, que se pode manexar co teclado e cos lectores de pantalla.
- **Informe**: como afinas ti, nota a nota. Na app, **gravar** e **escoitarse** en bucle.
- **Motor máis fino**: a nota cambia onde cambia a voz (nos glisandos rápidos quedaba atrás). O detector aguanta segundos harmónicos fortes, ruído e voz aireada sen saltar de oitava. As consoantes xa non estalan ao transpor e cambiar de xeito non dá golpe. En Directo, unha voz con vibrato e tremor xa non salta un intre á nota do lado.
- **Máis seguro**: un estado do DAW, un preset ou un .scl estragados xa non poden tirar o programa, e os .scl en Latin-1 (moitos de Windows) cárganse ben.

## Descargas

| Sistema | Instalador | Á man |
|---|---|---|
| **Mac** (Apple Silicon e Intel, macOS 11+) — AU, VST3, CLAP e app | `BarulloTune-…-mac.pkg` | `BarulloTune-…-mac.zip` |
| **Windows** (64 bits) — VST3, CLAP e app | `BarulloTune-…-windows-setup.exe` | `BarulloTune-…-windows-x64.zip` |
| **Linux** (x86_64) — VST3, LV2, CLAP e app | — | `BarulloTune-…-linux-x86_64.tar.gz` → `./instalar.sh` |

## Se o sistema avisa ao abrilo

- **Mac:** clic dereito sobre o instalador → *Abrir* → *Abrir*.
- **Windows:** «Windows protexeu o teu PC» → *Máis información* → *Executar de todos os xeitos*.

Avisan porque os instaladores non levan sinatura de pago, non porque haxa nada raro.
