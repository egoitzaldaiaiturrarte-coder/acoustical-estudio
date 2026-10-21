#!/usr/bin/env bash
# Compila (y opcionalmente ejecuta) el test del procesador VST
# (tools/test_plugin.cpp): el PluginProcessor.cpp REAL + el motor + el core C,
# enlazados contra los módulos JUCE reales, sin host y sin CMake (en Linux el
# CMake de JUCE obliga a construir juceaide con X11/freetype instalados).
#
# Uso:
#   build_plugintest.sh [salida]          → solo compila
#   build_plugintest.sh [salida] --run    → compila y ejecuta
# Variables:
#   JUCE_DIR      fuente de JUCE (por defecto build/_deps/juce-src)
#   JUCE_OBJ_DIR  caché de los .o de los módulos JUCE (por defecto
#                 /tmp/acoustical-juce-objs): solo se compilan la primera vez.
#
# Requisitos en Linux: g++, cabeceras de X11 y freetype (libx11-dev,
# libfreetype-dev). En runtime no se abre ninguna ventana ni se necesita
# display: el test no crea el editor.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
JUCE="${JUCE_DIR:-$ROOT/build/_deps/juce-src}"
OBJ="${JUCE_OBJ_DIR:-/tmp/acoustical-juce-objs}"
if [ ! -d "$JUCE/modules/juce_core" ]; then
    echo "JUCE no encontrado en $JUCE (pasa JUCE_DIR=/ruta/a/JUCE)" >&2
    exit 1
fi

OUT="${1:-/tmp/AcousticalPluginTest}"
DO_RUN=0
[ "${2:-}" = "--run" ] && DO_RUN=1

# Mismos defines que el target del plugin (JUCE_WEB_BROWSER=0, sin curl) más
# los que apagan lo que el test no usa y en Linux pediría librerías extra
# (ALSA/JACK, extensiones de X11). Los modal loops se permiten SOLO aquí
# para poder bombear el bucle de mensajes (runDispatchLoopUntil): es como el
# test deja correr los AsyncUpdater del procesador.
DEFS=(
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0
    -DJUCE_LOAD_CURL_SYMBOLS_LAZILY=0 -DJUCE_ALSA=0 -DJUCE_JACK=0 -DJUCE_BELA=0
    -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XSHM=0 -DJUCE_USE_XRENDER=0
    -DJUCE_USE_XCURSOR=0 -DJUCE_PLUGINHOST_VST3=0 -DJUCE_PLUGINHOST_LADSPA=0
    -DJUCE_PLUGINHOST_LV2=0 -DJUCE_USE_CAMERA=0 -DJUCE_MODAL_LOOPS_PERMITTED=1
    -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1
)
FLAGS=(-std=c++17 -O1 -fPIC -I"$JUCE/modules" -I/usr/include/freetype2 "${DEFS[@]}")

MODULES=(juce_core juce_events juce_data_structures juce_graphics juce_gui_basics
         juce_gui_extra juce_audio_basics juce_audio_devices juce_audio_formats
         juce_audio_processors juce_audio_utils)

mkdir -p "$OBJ"
JOBS="$(nproc 2>/dev/null || echo 2)"
echo "Módulos JUCE ($(basename "$JUCE")) → $OBJ (solo los que falten, $JOBS en paralelo)…"
pending=()
for m in "${MODULES[@]}"; do
    [ -f "$OBJ/$m.o" ] || pending+=("$m")
done
if [ "${#pending[@]}" -gt 0 ]; then
    printf '%s\n' "${pending[@]}" | xargs -P "$JOBS" -I{} \
        g++ "${FLAGS[@]}" -c "$JUCE/modules/{}/{}.cpp" -o "$OBJ/{}.o"
fi

MODULE_OBJS=()
for m in "${MODULES[@]}"; do MODULE_OBJS+=("$OBJ/$m.o"); done

echo "Compilando el test del plugin…"
gcc -std=c11 -O1 -fPIC -I"$REPO/dsp" -c "$REPO/dsp/acoustical_dsp.c" -o "$OBJ/acoustical_dsp_core.o"
g++ "${FLAGS[@]}" \
    -I"$ROOT/plugin" -I"$ROOT/engine" -I"$ROOT/app" -I"$REPO/dsp" \
    "$ROOT/tools/test_plugin.cpp" \
    "$ROOT/plugin/PluginProcessor.cpp" \
    "$ROOT/engine/FftProcessor.cpp" \
    "$ROOT/engine/SweeperProcessor.cpp" \
    "$ROOT/engine/EqDsp.cpp" \
    "$ROOT/engine/AcousticalEngine.cpp" \
    "$ROOT/engine/SignalGenerator.cpp" \
    "$OBJ/acoustical_dsp_core.o" \
    "${MODULE_OBJS[@]}" \
    -lfreetype -lpthread -ldl -lrt \
    -o "$OUT"
echo "Compilado: $OUT"

if [ "$DO_RUN" = "1" ]; then
    "$OUT"
fi
