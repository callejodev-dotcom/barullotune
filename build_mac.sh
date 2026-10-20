#!/bin/bash
# Compila e instala Barullo Tune en un Mac (Apple Silicon o Intel).
# Uso:  abre Terminal en esta carpeta y escribe   ./build_mac.sh
# Deja: VST3 en ~/Library/Audio/Plug-Ins/VST3, AU en ~/Library/Audio/Plug-Ins/Components
#       y la app independiente en ~/Applications.
set -eo pipefail
cd "$(dirname "$0")"

say() { printf "\n\033[1;33m==> %s\033[0m\n" "$*"; }

# 1. Herramientas: Xcode Command Line Tools y CMake
if ! xcode-select -p >/dev/null 2>&1; then
  say "Faltan las herramientas de compilación de Apple. Se abrirá el instalador: acéptalo y vuelve a ejecutar este script."
  xcode-select --install || true
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    say "Instalando CMake con Homebrew"
    brew install cmake
  else
    say "Falta CMake. Instálalo desde https://cmake.org/download/ (o con Homebrew: brew install cmake) y vuelve a ejecutar."
    exit 1
  fi
fi

# 2. JUCE (se descarga una vez, al lado de esta carpeta)
JUCE_DIR="${JUCE_DIR:-$(cd .. && pwd)/JUCE}"
if [ ! -f "$JUCE_DIR/CMakeLists.txt" ]; then
  say "Descargando JUCE 8.0.4"
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

# 3. Compilar (universal: arm64 + x86_64)
say "Compilando (tarda unos minutos la primera vez)"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
cmake -B build-mac ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$JUCE_DIR"
cmake --build build-mac --config Release -j "$(sysctl -n hw.ncpu)"

ART=build-mac/BarulloTune_artefacts/Release
[ -d "$ART" ] || ART=build-mac/BarulloTune_artefacts

# 4. Firmar en local (sin cuenta de desarrollador) e instalar
say "Instalando"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
APP_DIR="$HOME/Applications"
mkdir -p "$VST3_DIR" "$AU_DIR" "$APP_DIR"

install_bundle() {
  local src="$1" dst_dir="$2"
  local name; name="$(basename "$src")"
  rm -rf "$dst_dir/$name"
  cp -R "$src" "$dst_dir/"
  xattr -cr "$dst_dir/$name" 2>/dev/null || true
  codesign --force --deep --sign - "$dst_dir/$name"
  echo "   $dst_dir/$name"
}
install_bundle "$ART/VST3/Barullo Tune.vst3" "$VST3_DIR"
install_bundle "$ART/AU/Barullo Tune.component" "$AU_DIR"
install_bundle "$ART/Standalone/Barullo Tune.app" "$APP_DIR"

# 5. Que el Mac vuelva a buscar Audio Units y comprobar el AU
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
say "Comprobando el Audio Unit (auval)"
if auval -v aumf Btun Esco >/tmp/barullotune_auval.txt 2>&1; then
  echo "   AU validado correctamente"
else
  echo "   auval dio un aviso; detalles en /tmp/barullotune_auval.txt (el VST3 y la app funcionan igual)"
fi

say "Listo. Abre tu DAW y busca \"Barullo Tune\" en los efectos (VST3 o AU), o abre la app en ~/Applications."
