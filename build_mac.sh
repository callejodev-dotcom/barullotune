#!/bin/bash
# Compila e instala Barullo Tune nun Mac (Apple Silicon ou Intel).
# Uso:  abre o Terminal neste cartafol e escribe   ./build_mac.sh
# Deixa: VST3 en ~/Library/Audio/Plug-Ins/VST3, AU en ~/Library/Audio/Plug-Ins/Components,
#        CLAP en ~/Library/Audio/Plug-Ins/CLAP e a app independente en ~/Applications.
set -eo pipefail
cd "$(dirname "$0")"

say() { printf "\n\033[1;33m==> %s\033[0m\n" "$*"; }

# 1. Ferramentas: Xcode Command Line Tools e CMake
if ! xcode-select -p >/dev/null 2>&1; then
  say "Faltan as ferramentas de compilación de Apple. Abrirase o instalador: acéptao e volve executar este script."
  xcode-select --install || true
  exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    say "Instalando CMake con Homebrew"
    brew install cmake
  else
    say "Falta CMake. Instálao desde https://cmake.org/download/ (ou con Homebrew: brew install cmake) e volve executar."
    exit 1
  fi
fi

# 2. JUCE (descárgase unha vez, ao carón deste cartafol)
JUCE_DIR="${JUCE_DIR:-$(cd .. && pwd)/JUCE}"
if [ ! -f "$JUCE_DIR/CMakeLists.txt" ]; then
  say "Descargando JUCE 8.0.4"
  git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

# 3. Compilar (universal: arm64 + x86_64)
say "Compilando (a primeira vez tarda uns minutos)"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
cmake -B build-mac ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$JUCE_DIR"
cmake --build build-mac --config Release -j "$(sysctl -n hw.ncpu)"

ART=build-mac/BarulloTune_artefacts/Release
[ -d "$ART" ] || ART=build-mac/BarulloTune_artefacts

# 4. Asinar en local (sen conta de desenvolvedor) e instalar
say "Instalando"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
CLAP_DIR="$HOME/Library/Audio/Plug-Ins/CLAP"
APP_DIR="$HOME/Applications"
mkdir -p "$VST3_DIR" "$AU_DIR" "$CLAP_DIR" "$APP_DIR"

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
[ -d "$ART/CLAP/Barullo Tune.clap" ] && install_bundle "$ART/CLAP/Barullo Tune.clap" "$CLAP_DIR"
install_bundle "$ART/Standalone/Barullo Tune.app" "$APP_DIR"

# 5. Que o Mac volva buscar Audio Units, e comprobar o AU
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
say "Comprobando o Audio Unit (auval)"
if auval -v aumf Btun Esco >/tmp/barullotune_auval.txt 2>&1; then
  echo "   AU comprobado, todo ben"
else
  echo "   auval deu un aviso; detalles en /tmp/barullotune_auval.txt (o VST3, o CLAP e a app funcionan igual)"
fi

say "Feito. Abre o teu DAW e busca \"Barullo Tune\" nos efectos (VST3, AU ou CLAP), ou abre a app en ~/Applications."
