#!/bin/bash
# Empaqueta Barullo Tune para Mac: un instalador .pkg (universal: Apple Silicon + Intel) y un .zip con los bundles.
# Uso: packaging/package_mac.sh <carpeta de artefactos Release> <versión> <carpeta de salida>
#
# Firma: si existen estas variables de entorno, firma con tu certificado de Apple y notariza
# (así se abre sin avisos); si no, firma "ad hoc" (funciona, pero macOS avisará la primera vez):
#   MACOS_DEV_ID         "Developer ID Application: Nombre (EQUIPO)"
#   MACOS_INSTALLER_ID   "Developer ID Installer: Nombre (EQUIPO)"
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   (para notarizar)
set -eo pipefail
trap 'echo "::error::$(basename "$0") línea $LINENO: $BASH_COMMAND"' ERR
ART="$1"; VER="$2"; OUT="$3"
NAME="Barullo Tune"
mkdir -p "$OUT"
WORK="$(mktemp -d)"
ROOT="$WORK/root"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$ROOT/Library/Audio/Plug-Ins/CLAP" "$ROOT/Applications"
cp -R "$ART/VST3/$NAME.vst3"       "$ROOT/Library/Audio/Plug-Ins/VST3/"
[ -d "$ART/CLAP/$NAME.clap" ] && cp -R "$ART/CLAP/$NAME.clap" "$ROOT/Library/Audio/Plug-Ins/CLAP/"
cp -R "$ART/AU/$NAME.component"    "$ROOT/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/$NAME.app"  "$ROOT/Applications/"

# comprobar que es universal
lipo -info "$ROOT/Applications/$NAME.app/Contents/MacOS/$NAME"

SIGN_ID="${MACOS_DEV_ID:--}"
for b in "$ROOT/Library/Audio/Plug-Ins/VST3/$NAME.vst3" "$ROOT/Library/Audio/Plug-Ins/Components/$NAME.component" "$ROOT/Library/Audio/Plug-Ins/CLAP/$NAME.clap" "$ROOT/Applications/$NAME.app"; do
  [ -e "$b" ] || continue
  if [ "$SIGN_ID" = "-" ]; then
    codesign --force --deep --sign - "$b"
  else
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$b"
  fi
done

# .pkg: que el instalador no "reubique" los bundles si encuentra otra copia en el disco
pkgbuild --analyze --root "$ROOT" "$WORK/comp.plist"
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/comp.plist" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/comp.plist" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/comp.plist"
  i=$((i+1))
done
pkgbuild --root "$ROOT" --component-plist "$WORK/comp.plist" --identifier link.escoita.barullotune \
         --version "$VER" --install-location / "$WORK/BarulloTune-component.pkg"

PKG="$OUT/BarulloTune-$VER-mac.pkg"
if [ -n "$MACOS_INSTALLER_ID" ]; then
  productbuild --package "$WORK/BarulloTune-component.pkg" --sign "$MACOS_INSTALLER_ID" "$PKG"
else
  productbuild --package "$WORK/BarulloTune-component.pkg" "$PKG"
fi

# notarizar (solo con cuenta de desarrollador de Apple)
if [ -n "$APPLE_ID" ] && [ -n "$APPLE_TEAM_ID" ] && [ -n "$APPLE_APP_PASSWORD" ] && [ -n "$MACOS_INSTALLER_ID" ]; then
  xcrun notarytool submit "$PKG" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
  xcrun stapler staple "$PKG"
fi

# .zip con los bundles sueltos, para quien prefiera copiarlos a mano
ZIPDIR="$WORK/BarulloTune-$VER-mac"
mkdir -p "$ZIPDIR"
cp -R "$ROOT/Library/Audio/Plug-Ins/VST3/$NAME.vst3" "$ROOT/Library/Audio/Plug-Ins/Components/$NAME.component" "$ROOT/Applications/$NAME.app" "$ZIPDIR/"
[ -d "$ROOT/Library/Audio/Plug-Ins/CLAP/$NAME.clap" ] && cp -R "$ROOT/Library/Audio/Plug-Ins/CLAP/$NAME.clap" "$ZIPDIR/"
cp "$(dirname "$0")/LEEME-mac.txt" "$ZIPDIR/LEEME.txt"
(cd "$WORK" && ditto -c -k --sequesterRsrc --keepParent "BarulloTune-$VER-mac" "$OUT/BarulloTune-$VER-mac.zip")
echo "Hecho: $PKG y $OUT/BarulloTune-$VER-mac.zip"
