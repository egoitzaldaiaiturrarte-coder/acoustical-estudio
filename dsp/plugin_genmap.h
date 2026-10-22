/* plugin_genmap.h — El menú "Generador" del plugin VST (6 opciones) y la
 * migración one-shot del estado antiguo guardado con el menú de 5 opciones
 * anterior (v1.8 y anteriores).
 *
 * Orden canónico del menú generador en las tres versiones (app Windows,
 * VST, Android): "Silencio" es SIEMPRE la primera opción y la única que
 * nunca suena:
 *
 *   Menú nuevo (v1.9, 6 opciones):
 *     0 = Silencio     — generador APAGADO: la entrada pasa tal cual (v1.10.2;
 *                        lo mismo que "Silencio" en la app Windows y el móvil,
 *                        y lo que hacía el plugin por defecto hasta la v1.7)
 *     1 = Apagado      — lo mismo (se conserva: proyectos guardados con él)
 *     2 = Ruido rosa   — ACOUSTICAL_WAVE_PINK
 *     3 = Ruido blanco — ACOUSTICAL_WAVE_WHITE
 *     4 = Barrido      — ACOUSTICAL_WAVE_LOG_SWEEP
 *     5 = Seno         — ACOUSTICAL_WAVE_SINE
 *
 *   Menú antiguo (v1.8 y anteriores, 5 opciones):
 *     0 = Apagado, 1 = Ruido rosa, 2 = Ruido blanco, 3 = Barrido, 4 = Seno
 *
 * QUÉ VALOR LLEGA (v1.10.1): el APVTS guarda y devuelve el valor
 * DESNORMALIZADO de cada parámetro — en un AudioParameterChoice eso es el
 * ÍNDICE de la opción (0..5), tanto en getRawParameterValue() como en la
 * propiedad "value" del estado guardado. Solo el host ve la escala 0-1.
 * La v1.9 asumió lo contrario (crudo = índice/5) y multiplicaba por 5: todas
 * las opciones menos Silencio acababan en "Seno" (a +5 dBFS de pico con la
 * ganancia espacial de entonces), y la migración mandaba a Silencio los
 * proyectos v1.8 con Blanco/Barrido/Seno. test_plugin.cpp lo comprueba ahora
 * contra el procesador real.
 *
 * v1.10.2: en la v1.9-v1.10.1 "Silencio" escribía CEROS en la salida y era
 * la opción por defecto: insertar el plugin enmudecía la pista. En la app y
 * en el móvil "Silencio" siempre ha sido "sin señal añadida, la entrada pasa".
 *
 * MIGRACIÓN: el estado guardado por el host que no lleva el marcador de
 * propiedad "genSourceMenu" es de v1.8 o anterior: su índice k pasa a ser
 * k + 1 en el menú nuevo, de modo que un proyecto antiguo sigue sonando
 * EXACTAMENTE lo que sonaba (Apagado sigue Apagado, …, Seno sigue Seno). El
 * estado guardado por la versión nueva SIEMPRE lleva el marcador (lo escribe
 * getStateInformation), así que la migración es one-shot.
 *
 * Funciones puras (sin JUCE, sin allocations): el gate DSP lo testa
 * (test_genmap.cpp) y el plugin compila este mismo fichero tal cual, para
 * que el mapeo no pueda divergir de su test.
 */
#ifndef ACOUSTICAL_PLUGIN_GENMAP_H
#define ACOUSTICAL_PLUGIN_GENMAP_H

#include "acoustical_dsp.h"

#include <cmath>

namespace acoustical {
namespace genmenu {

/* Número de opciones del menú generador nuevo. */
constexpr int kMenuCount = 6;

/*
 * Índice de opción del menú NUEVO → valor de forma de onda canónica del
 * core (ACOUSTICAL_WAVE_*). Devuelve -1 para Silencio/Apagado (passthrough: no
 * interviene el generador; la entrada pasa tal cual y no deja huella en el
 * anillo pre-EQ). Fuera de rango también devuelve -1: la rama más segura es
 * nunca emitir una señal equivocada.
 */
inline int waveformForMenuIndex(int index) {
    switch (index) {
        case 0:  return -1;                          /* Silencio: generador apagado (passthrough) */
        case 1:  return -1;                          /* Apagado: passthrough (compatibilidad) */
        case 2:  return ACOUSTICAL_WAVE_PINK;
        case 3:  return ACOUSTICAL_WAVE_WHITE;
        case 4:  return ACOUSTICAL_WAVE_LOG_SWEEP;
        case 5:  return ACOUSTICAL_WAVE_SINE;
        default: return -1;
    }
}

/* Opción del menú a la que se vuelve ante un valor corrupto: "Apagado"
 * (passthrough), el comportamiento por defecto del plugin hasta la v1.8 —
 * un valor ilegible no puede ni enmudecer la pista ni meter una señal. */
constexpr int kFallbackIndex = 1;

/*
 * Valor del parámetro "genSource" tal como lo da el APVTS (el índice como
 * float) → índice de opción 0..5. Redondea (el host puede devolver 2.9999)
 * y acota; no finito → kFallbackIndex.
 */
inline int menuIndexFromValue(float value) {
    if (!std::isfinite((double)value)) return kFallbackIndex;
    const long i = std::lround(value);
    if (i < 0) return 0;
    if (i > kMenuCount - 1) return kMenuCount - 1;
    return static_cast<int>(i);
}

/*
 * Migración one-shot de un índice del menú antiguo de 5 opciones (el
 * "value" guardado en un estado SIN marcador) al índice del menú nuevo:
 * k → k + 1 (Apagado 0→1, Rosa 1→2, Blanco 2→3, Barrido 3→4, Seno 4→5).
 * Un valor que no es un índice antiguo válido (no finito, fuera de 0..4 o
 * no entero) → kFallbackIndex.
 */
inline float migrateLegacyIndex(float value) {
    if (!std::isfinite((double)value)) return static_cast<float>(kFallbackIndex);
    for (int k = 0; k <= 4; ++k) {
        if (std::fabs(value - static_cast<float>(k)) <= 1e-3f)
            return static_cast<float>(k + 1);
    }
    return static_cast<float>(kFallbackIndex);
}

}  // namespace genmenu
}  // namespace acoustical

#endif /* ACOUSTICAL_PLUGIN_GENMAP_H */
