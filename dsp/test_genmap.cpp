// test_genmap.cpp — el menú "Generador" del plugin (v1.9: 6 opciones con
// "Silencio" primero, la unificada con la app y el móvil) y la migración
// one-shot del estado guardado con el menú antiguo de 5 opciones (v1.8 y
// anteriores). Fija dos cosas:
//   - el orden canónico del menú en las tres versiones: 0 = Silencio (la
//     única que nunca suena), 1 = Apagado (passthrough, solo VST) y
//     2..5 = Rosa/Blanco/Barrido/Seno mapeados a los valores canónicos del
//     core (4/5/3/1);
//   - la migración de valores crudos del host: un proyecto viejo conserva
//     EXACTAMENTE lo que sonaba (Apagado→Apagado, Rosa→Rosa, …, Seno→Seno)
//     y la migración es idempotente sobre la rejilla nueva.
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
    CHECK(waveformForMenuIndex(0) == ACOUSTICAL_WAVE_SILENCE,
          "0 = Silencio (primera opción; la única que nunca suena)");
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

    // === La migración desplaza cada opción vieja a su equivalente nuevo ===
    // Menú viejo: crudo k/4 → opciones [Apagado, Rosa, Blanco, Barrido, Seno].
    // Menú nuevo: crudo k/5 → [Silencio, Apagado, Rosa, Blanco, Barrido, Seno].
    CHECK(nearF(migrateRawValue(0.0f), 0.2f), "viejo Apagado (0) → nuevo Apagado (0.2)");
    CHECK(nearF(migrateRawValue(0.25f), 0.4f), "viejo Rosa (0.25) → nuevo Rosa (0.4)");
    CHECK(nearF(migrateRawValue(0.5f), 0.6f), "viejo Blanco (0.5) → nuevo Blanco (0.6)");
    CHECK(nearF(migrateRawValue(0.75f), 0.8f), "viejo Barrido (0.75) → nuevo Barrido (0.8)");
    CHECK(nearF(migrateRawValue(1.0f), 1.0f), "viejo Seno (1.0) → nuevo Seno (1.0)");

    // === Idempotencia: la rejilla nueva no se toca ===
    CHECK(nearF(migrateRawValue(0.2f), 0.2f), "nuevo Apagado (0.2) → inalterado");
    CHECK(nearF(migrateRawValue(0.4f), 0.4f), "nuevo Rosa (0.4) → inalterado");
    CHECK(nearF(migrateRawValue(0.6f), 0.6f), "nuevo Blanco (0.6) → inalterado");
    CHECK(nearF(migrateRawValue(0.8f), 0.8f), "nuevo Barrido (0.8) → inalterado");

    // === Valores corruptos → Silencio (la salida más segura) ===
    CHECK(nearF(migrateRawValue(-0.1f), 0.0f), "crudo < 0 → Silencio");
    CHECK(nearF(migrateRawValue(1.5f), 0.0f), "crudo > 1 → Silencio");
    CHECK(nearF(migrateRawValue(std::nanf("")), 0.0f), "crudo NaN → Silencio");
    CHECK(nearF(migrateRawValue(std::numeric_limits<float>::infinity()), 0.0f),
          "crudo Inf → Silencio");

    // === isLegacyRawValue: solo la rejilla de 5 opciones ===
    CHECK(isLegacyRawValue(0.0f), "0.0 es de la rejilla vieja");
    CHECK(isLegacyRawValue(0.25f), "0.25 es de la rejilla vieja");
    CHECK(isLegacyRawValue(0.5f), "0.5 es de la rejilla vieja");
    CHECK(isLegacyRawValue(0.75f), "0.75 es de la rejilla vieja");
    CHECK(isLegacyRawValue(1.0f), "1.0 es de la rejilla vieja");
    CHECK(!isLegacyRawValue(0.2f), "0.2 NO es de la rejilla vieja");
    CHECK(!isLegacyRawValue(0.4f), "0.4 NO es de la rejilla vieja");
    CHECK(!isLegacyRawValue(0.6f), "0.6 NO es de la rejilla vieja");
    CHECK(!isLegacyRawValue(0.8f), "0.8 NO es de la rejilla vieja");
    CHECK(!isLegacyRawValue(0.1f), "0.1 NO es de la rejilla vieja");

    if (g_fail == 0) {
        std::printf("test_genmap: %d checks OK\n", g_checks);
        return 0;
    }
    std::printf("test_genmap: %d/%d FALLARON\n", g_fail, g_checks);
    return 1;
}
