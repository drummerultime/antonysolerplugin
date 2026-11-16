#!/bin/bash
# Compile FRAGOR (VST3 + AU + Standalone) sur macOS et installe les plugins
# dans ~/Library/Audio/Plug-Ins. Prérequis : Xcode Command Line Tools + CMake 3.22+
#   xcode-select --install
#   brew install cmake
set -e
cd "$(dirname "$0")"

if [ ! -d JUCE ]; then
    echo "==> Téléchargement de JUCE 8.0.4"
    git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
fi

JOBS=$(sysctl -n hw.ncpu)
echo "==> Configuration"
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "==> Compilation ($JOBS cœurs)"
cmake --build build --config Release --target Fragor_All -j "$JOBS"

echo "==> Rafraîchissement du cache Audio Units"
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Installé :"
echo "  ~/Library/Audio/Plug-Ins/VST3/FRAGOR.vst3"
echo "  ~/Library/Audio/Plug-Ins/Components/FRAGOR.component"
echo "  Standalone : build/Fragor_artefacts/Release/Standalone/FRAGOR.app"
echo ""
echo "Vérification AU (optionnel) : auval -v aumu Frgr Asol"
