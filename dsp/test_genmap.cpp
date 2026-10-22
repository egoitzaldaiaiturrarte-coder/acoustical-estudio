// test_genmap.cpp — el menú "Generador" del plugin (v1.9: 6 opciones con
// "Silencio" primero, la unificada con la app y el móvil) y la migración
// one-shot del estado guardado con el menú antiguo de 5 opciones (v1.8 y
// anteriores). Fija dos cosas:
//   - el orden canónico del menú en las tres versiones: 0 = Silencio (la
//     única que nunca suena), 1 = Apagado (passthrough, solo VST) y
//     2..5 = Rosa/Blanco/Barrido/Seno mapeados a los valores canónicos del
//     core (4/5/3/1);
//   - el valor que entrega el APVTS es el ÍNDICE de la opción (no 0..1:
//     ese error de la v1.9 mandaba todo el menú a "Seno");
//   - la migración de proyectos v1.8: el índice k viejo es k + 1 en el
//     nuevo, así un proyecto conserva EXACTAMENTE lo que sonaba
//     (Apagado→Apagado, Rosa→Rosa, …, Seno→Seno).
// El procesador real (APVTS incluido) lo cubre tools/test_plugin.cpp.
#include "plugin_genmap.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; std::printf("FAIL: %s\n", msg); } \
} while (0)

static bool nearF(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    using namespace acoustical::genmenu;

    // === El menú nuevo mapea a las formas canónicas del core ===
    CHECK(waveformForMenuIndex(0) == -1,
          "0 = Silencio (primera opción): generador apagado, la entrada pasa (v1.10.2)");
    CHECK(waveformForMenuIndex(1) == -1,
          "1 = Apagado (passthrough: el generador no interviene)");
    CHECK(waveformForMenuIndex(2) == ACOUSTICAL_WAVE_PINK, "2 = Ruido rosa");
    CHECK(waveformForMenuIndex(3) == ACOUSTICAL_WAVE_WHITE, "3 = Ruido blanco");
    CHECK(waveformForMenuIndex(4) == ACOUSTICAL_WAVE_LOG_SWEEP, "4 = Barrido");
    CHECK(waveformForMenuIndex(5) == ACOUSTICAL_WAVE_SINE, "5 = Seno");

    // Fuera de rango (valor corrupto): passthrough, nunca una señal equivocada.
    CHECK(waveformForMenuIndex(-1) == -1, "fuera de rango (-1) → passthrough");
    CHECK(waveformForMenuIndex(6) == -1, "fuera de rango (6) → passthrough");
    CHECK(waveformForMenuIndex(99) == -1, "fuera de rango (99) → passthrough");

    // === Valor del APVTS (índice como float) → opción ===
    for (int i = 0; i < kMenuCount; ++i)
        CHECK(menuIndexFromValue(static_cast<float>(i)) == i, "índice exacto → la misma opción");
    CHECK(menuIndexFromValue(4.9999f) == 5, "4.9999 redondea a 5 (Seno)");
    CHECK(menuIndexFromValue(1.0001f) == 1, "1.0001 redondea a 1 (Apagado)");
    CHECK(menuIndexFromValue(0.2f) == 0, "0.2 es el índice 0, NO la opción 1 (el error de v1.9)");
    CHECK(menuIndexFromValue(1.0f) == 1, "1.0 es Apagado, NO Seno (el error de v1.9)");
    CHECK(menuIndexFromValue(-3.0f) == 0, "negativo → 0");
    CHECK(menuIndexFromValue(42.0f) == kMenuCount - 1, "por encima → la última");
    CHECK(menuIndexFromValue(std::nanf("")) == kFallbackIndex, "NaN → Apagado (passthrough)");
    CHECK(waveformForMenuIndex(kFallbackIndex) == -1, "el fallback es passthrough");

    // === Migración v1.8 → v1.9: índice k → k + 1 ===
    // Menú viejo: [Apagado, Rosa, Blanco, Barrido, Seno].
    // Menú nuevo: [Silencio, Apagado, Rosa, Blanco, Barrido, Seno].
    CHECK(nearF(migrateLegacyIndex(0.0f), 1.0f), "viejo Apagado (0) → nuevo Apagado (1)");
    CHECK(nearF(migrateLegacyIndex(1.0f), 2.0f), "viejo Rosa (1) → nuevo Rosa (2)");
    CHECK(nearF(migrateLegacyIndex(2.0f), 3.0f), "viejo Blanco (2) → nuevo Blanco (3)");
    CHECK(nearF(migrateLegacyIndex(3.0f), 4.0f), "viejo Barrido (3) → nuevo Barrido (4)");
    CHECK(nearF(migrateLegacyIndex(4.0f), 5.0f), "viejo Seno (4) → nuevo Seno (5)");
    CHECK(nearF(migrateLegacyIndex(2.0004f), 3.0f), "tolera el redondeo del XML");

    // La opción migrada suena lo que sonaba: misma forma canónica (o passthrough)
    const int oldMenuWave[5] = { -1, ACOUSTICAL_WAVE_PINK, ACOUSTICAL_WAVE_WHITE,
                                 ACOUSTICAL_WAVE_LOG_SWEEP, ACOUSTICAL_WAVE_SINE };
    for (int k = 0; k < 5; ++k)
        CHECK(waveformForMenuIndex(menuIndexFromValue(migrateLegacyIndex((float)k))) == oldMenuWave[k],
              "la opción migrada suena igual que en v1.8");

    // === Valores corruptos → Apagado (passthrough: ni enmudece ni mete señal) ===
    CHECK(nearF(migrateLegacyIndex(-1.0f), 1.0f), "índice < 0 → Apagado");
    CHECK(nearF(migrateLegacyIndex(5.0f), 1.0f), "índice 5 no existía en v1.8 → Apagado");
    CHECK(nearF(migrateLegacyIndex(0.5f), 1.0f), "no entero → Apagado");
    CHECK(nearF(migrateLegacyIndex(std::nanf("")), 1.0f), "NaN → Apagado");
    CHECK(nearF(migrateLegacyIndex(std::numeric_limits<float>::infinity()), 1.0f),
          "Inf → Apagado");

    if (g_fail == 0) {
        std::printf("test_genmap: %d checks OK\n", g_checks);
        return 0;
    }
    std::printf("test_genmap: %d/%d FALLARON\n", g_fail, g_checks);
    return 1;
}
