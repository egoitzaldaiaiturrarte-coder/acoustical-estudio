/* plugin_genmap.h — El menú "Generador" del plugin VST (6 opciones) y la
 * migración one-shot del estado antiguo guardado con el menú de 5 opciones
 * anterior (v1.8 y anteriores).
 *
 * Orden canónico del menú generador en las tres versiones (app Windows,
 * VST, Android): "Silencio" es SIEMPRE la primera opción y la única que
 * nunca suena:
 *
 *   Menú nuevo (v1.9, 6 opciones; el valor crudo del parámetro VST3/VST2
 *   del host es índice/5):
 *     0 = Silencio     — escribe CEROS en la salida (conmutador unificado:
 *                        el anillo pre-EQ de referencia también ve ceros,
 *                        el generador no deja huella)
 *     1 = Apagado      — passthrough: la entrada pasa tal cual (solo VST)
 *     2 = Ruido rosa   — ACOUSTICAL_WAVE_PINK
 *     3 = Ruido blanco — ACOUSTICAL_WAVE_WHITE
 *     4 = Barrido      — ACOUSTICAL_WAVE_LOG_SWEEP
 *     5 = Seno         — ACOUSTICAL_WAVE_SINE
 *
 *   Menú antiguo (v1.8 y anteriores, 5 opciones; crudo = índice/4):
 *     0 = Apagado, 1 = Ruido rosa, 2 = Ruido blanco, 3 = Barrido, 4 = Seno
 *
 * MIGRACIÓN: el estado guardado por el host que no lleva el marcador de
 * propiedad "genSourceMenu" es de v1.8 o anterior. Cada valor crudo k/4 del
 * menú antiguo se desplaza a (k+1)/5 del menú nuevo (índice +1), de modo que
 * un proyecto antiguo sigue sonando EXACTAMENTE lo que sonaba: Apagado sigue
 * Apagado, Rosa sigue Rosa, …, Seno (crudo 1.0 en ambos menús) sigue Seno.
 * El estado guardado por la versión nueva SIEMPRE lleva el marcador (lo
 * escribe getStateInformation), así que la migración es one-shot e
 * idempotente (ver migrateRawValue).
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
 * core (ACOUSTICAL_WAVE_*). Devuelve -1 para "Apagado" (passthrough: no
 * interviene el generador; la entrada pasa tal cual y no deja huella en el
 * anillo pre-EQ). Fuera de rango también devuelve -1: la rama más segura es
 * nunca emitir una señal equivocada.
 */
inline int waveformForMenuIndex(int index) {
    switch (index) {
        case 0:  return ACOUSTICAL_WAVE_SILENCE;    /* Silencio: la que nunca suena */
        case 1:  return -1;                          /* Apagado: passthrough */
        case 2:  return ACOUSTICAL_WAVE_PINK;
        case 3:  return ACOUSTICAL_WAVE_WHITE;
        case 4:  return ACOUSTICAL_WAVE_LOG_SWEEP;
        case 5:  return ACOUSTICAL_WAVE_SINE;
        default: return -1;
    }
}

/*
 * True si `raw` (0-1) cae sobre la rejilla del menú antiguo de 5 opciones
 * {0, 1/4, 1/2, 3/4, 1}. El epsilon absorbe el redondeo del XML del estado
 * del host (el valor se serializa con decimales limitados).
 */
inline bool isLegacyRawValue(float raw) {
    for (int k = 0; k <= 4; ++k) {
        if (std::fabs(raw - k / 4.0f) <= 1e-4f) return true;
    }
    return false;
}

/*
 * Migración one-shot de un valor crudo 0-1 del menú antiguo de 5 opciones al
 * menú nuevo de 6:
 *   - crudo k/4 de la rejilla antigua → (k+1)/5: la MISMA opción en el menú
 *     nuevo (Apagado 0→1, Rosa 1→2, Blanco 2→3, Barrido 3→4, Seno 4→5).
 *   - crudo de la rejilla nueva {0, 1/5, 2/5, 3/5, 4/5, 1} que NO coincide
 *     con la antigua → inalterado: idempotente para estado nuevo (el caller
 *     solo invoca la migración sobre estados sin marcador; ver abajo).
 *   - valor corrupto (no finito o fuera de [0,1]) → 0 (Silencio): la salida
 *     más segura, nunca una señal equivocada.
 *
 * Nota: 0.0 y 1.0 pertenecen a AMBAS rejillas. El caller solo migra estados
 * sin marcador (v1.8 o anteriores), donde 0.0 significa "Apagado" y debe
 * migrar a 0.2; un 0.0 de estado nuevo nunca pasa por aquí porque su XML ya
 * lleva el marcador.
 */
inline float migrateRawValue(float raw) {
    if (!std::isfinite((double)raw) || raw < 0.0f || raw > 1.0f) return 0.0f;
    for (int k = 0; k <= 4; ++k) {
        if (std::fabs(raw - k / 4.0f) <= 1e-4f) return (k + 1) / 5.0f;
    }
    return raw;
}

}  // namespace genmenu
}  // namespace acoustical

#endif /* ACOUSTICAL_PLUGIN_GENMAP_H */
