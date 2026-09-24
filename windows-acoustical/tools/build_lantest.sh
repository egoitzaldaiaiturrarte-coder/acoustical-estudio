#!/usr/bin/env bash
# Compila (y opcionalmente ejecuta) el test de protocolo del enlace móvil↔PC.
#
# Compila con g++ directo los MISMOS .cpp que enlaza la app de Windows
# (SyncClient.cpp, BeaconListener.cpp) junto a los módulos JUCE que usan
# (core, events, cryptography) — sin el sistema CMake de JUCE, que en Linux
# obliga a instalar X11/freetype para construir su herramienta interna.
#
# Uso:
#   build_lantest.sh [salida]            → solo compila
#   build_lantest.sh [salida] --run      → compila y ejecuta
# JUCE_DIR: ruta al source de JUCE (por defecto, el que ya bajó el build
# principal de la app: windows-acoustical/build/_deps/juce-src).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
JUCE="${JUCE_DIR:-$ROOT/build/_deps/juce-src}"
if [ ! -d "$JUCE/modules/juce_core" ]; then
    echo "JUCE no encontrado en $JUCE (pasa JUCE_DIR=/ruta/a/JUCE)" >&2
    exit 1
fi

OUT="${1:-/tmp/AcousticalLanTest}"
DO_RUN=0
[ "${2:-}" = "--run" ] && DO_RUN=1

echo "Compilando test de protocolo (JUCE $(basename "$JUCE"))…"
g++ -std=c++17 -O1 \
    "$ROOT/tools/test_lan_link.cpp" \
    "$ROOT/app/SyncClient.cpp" \
    "$ROOT/app/BeaconListener.cpp" \
    "$JUCE/modules/juce_core/juce_core.cpp" \
    "$JUCE/modules/juce_events/juce_events.cpp" \
    "$JUCE/modules/juce_cryptography/juce_cryptography.cpp" \
    -I"$JUCE/modules" -I"$JUCE" \
    -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_LOAD_CURL_SYMBOLS_LAZILY=0 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -fPIC -lpthread -ldl \
    -o "$OUT"
echo "Compilado: $OUT"

# PhoneLink.cpp: solo compila en su totalidad en Windows (usa el registro y
# ShellExecute), pero la lógica de transporte es multiplataforma y debe
# compilarse aquí para pillar errores antes del job de Windows.
g++ -std=c++17 -fsyntax-only \
    -I"$JUCE/modules" -I"$JUCE" -I"$ROOT/app" \
    -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_LOAD_CURL_SYMBOLS_LAZILY=0 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    "$ROOT/app/PhoneLink.cpp"
echo "PhoneLink.cpp: syntax OK"

if [ "$DO_RUN" = "1" ]; then
    "$OUT"
fi
