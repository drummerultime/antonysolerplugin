#!/bin/bash
# Fabrique un installeur macOS (.pkg) d'Alea + Talea + Color + Cantus a partir des plugins compiles.
# Usage : bash scripts/make_installer_mac.sh build
# Signature optionnelle : exporter ALEA_INSTALLER_IDENTITY="Developer ID Installer: Nom (TEAMID)"
set -euo pipefail

BUILD="${1:-build}"
OUT="$BUILD/AleaSuite-macOS.pkg"
WORK="$(mktemp -d)"
VERSION="5.0.0"
PKGS=()

for P in Alea Talea Color Cantus; do
    ART="$BUILD/${P}_artefacts/Release"
    LOW="$(echo "$P" | tr '[:upper:]' '[:lower:]')"
    mkdir -p "$WORK/$P/vst3" "$WORK/$P/au" "$WORK/$P/app"
    [ -d "$ART/VST3/$P.vst3" ]      && cp -R "$ART/VST3/$P.vst3" "$WORK/$P/vst3/"
    [ -d "$ART/AU/$P.component" ]   && cp -R "$ART/AU/$P.component" "$WORK/$P/au/"
    [ -d "$ART/Standalone/$P.app" ] && cp -R "$ART/Standalone/$P.app" "$WORK/$P/app/"
    pkgbuild --root "$WORK/$P/vst3" --identifier "fr.antonysoler.$LOW.vst3" --version "$VERSION" \
             --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/$LOW-vst3.pkg"
    pkgbuild --root "$WORK/$P/au" --identifier "fr.antonysoler.$LOW.au" --version "$VERSION" \
             --install-location "/Library/Audio/Plug-Ins/Components" "$WORK/$LOW-au.pkg"
    pkgbuild --root "$WORK/$P/app" --identifier "fr.antonysoler.$LOW.app" --version "$VERSION" \
             --install-location "/Applications" "$WORK/$LOW-app.pkg"
    PKGS+=(--package "$WORK/$LOW-vst3.pkg" --package "$WORK/$LOW-au.pkg" --package "$WORK/$LOW-app.pkg")
done

SIGN=()
if [ -n "${ALEA_INSTALLER_IDENTITY:-}" ]; then SIGN=(--sign "$ALEA_INSTALLER_IDENTITY"); fi
productbuild ${SIGN[@]+"${SIGN[@]}"} "${PKGS[@]}" "$OUT"
echo "Installeur : $OUT"
