// test_modes.cpp — el catálogo de modos del núcleo compartido (los 6 perfiles
// de arranque: Coche Normal / Coche Aire libre / Estudio-Esc 1 / Esc 2 /
// Esc 3 / PAs). Fija los valores acordados para que las tres versiones (el
// catálogo C que leen Windows y el VST, y la copia Kotlin de Android) no
// puedan divergir: si alguien cambia una frecuencia, un SPL o una banda del
// banco, este test obliga a actualizar también el fixture espejo.
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; std::printf("FAIL: %s\n", msg); } \
} while (0)

static bool nearF(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}

static const acoustical_mode_profile* byId(const char* id) {
    return acoustical_mode_by_id(id);
}

int main() {
    const acoustical_mode_profile* all = acoustical_mode_profiles();
    CHECK(all != nullptr, "el catálogo existe");
    if (!all) return 1;

    // Invariantes comunes a los SEIS modos (acordado: todos arrancan con
    // ganancia máx 50 dB, 124 bandas · 2048 FFT · análisis 25 ms, suavizado
    // 0.05, generador en Silencio y los 3 ecuas activos).
    for (int i = 0; i < ACOUSTICAL_MODE_COUNT; ++i) {
        const auto& m = all[i];
        std::printf("  %-16s  SPL %.0f  corr=%d sub=%d  z=%.1f size=%.1f\n",
                     m.id, m.targetSpl, m.correctionEnabled, m.noiseSubtractionEnabled,
                     m.spatialZ, m.spatialSize);
        CHECK(m.id != nullptr && m.name != nullptr, "id/name presentes");
        CHECK(nearF(m.maxGainDb, 50.0f), "ganancia máx 50 dB");
        CHECK(m.bandCountIndex == 4, "124 bandas (Ultra)");
        CHECK(m.fftSizeIndex == 2, "FFT 2048");
        CHECK(m.analysisIntervalIndex == 0, "análisis 25 ms");
        CHECK(nearF(m.smoothingFactor, 0.05f), "suavizado 0.05");
        CHECK(m.generatorWaveform == ACOUSTICAL_WAVE_SILENCE, "generador en Silencio");
        for (int e = 0; e < 3; ++e) {
            CHECK(m.eqs[e].enabled == 1, "los 3 ecuas dinámicos activos");
            CHECK(m.eqs[e].intervalMs > 0 && m.eqs[e].intervalMs <= 2000, "intervalo en rango");
            for (int k = 0; k < 4; ++k) {
                const auto& b = m.eqs[e].support[k];
                CHECK(b.frequencyHz > 0.0f && b.frequencyHz <= 20000.0f, "banda de apoyo en rango");
                CHECK(b.gainDb == 0.0f, "banda de apoyo neutra (0 dB) al arrancar");
                CHECK(nearF(b.q, 2.0f), "Q 2 en las bandas de apoyo");
            }
        }
        CHECK(m.spatialX >= -1.0f && m.spatialX <= 1.0f, "x en [-1,1]");
        CHECK(m.spatialY >= -1.0f && m.spatialY <= 1.0f, "y en [-1,1]");
        CHECK(m.spatialZ >= 0.0f && m.spatialZ <= 1.0f, "z en [0,1]");
        CHECK(m.spatialSize >= 0.0f && m.spatialSize <= 1.0f, "size en [0,1]");
    }

    // Los seis SPL acordados (el de PAs es 103: exige el rango 30–110 unificado)
    struct Expect { const char* id; float spl; int corr; int sub; float z, size; };
    const Expect expect[ACOUSTICAL_MODE_COUNT] = {
        {"carro-normal",     78.0f, 1, 1, 0.2f, 0.3f},
        {"carro-aire-libre", 97.0f, 0, 1, 0.5f, 0.5f},
        {"estudio-esc1",     89.0f, 1, 1, 0.5f, 0.5f},
        {"escenario2",       91.0f, 0, 0, 0.5f, 0.5f},
        {"escenario3",       93.0f, 1, 1, 0.9f, 0.9f},
        {"pas",              103.0f, 1, 1, 0.2f, 0.3f},
    };
    for (const auto& e : expect) {
        const auto* m = byId(e.id);
        CHECK(m != nullptr, e.id);
        if (!m) continue;
        CHECK(nearF(m->targetSpl, e.spl), e.id);
        CHECK(m->correctionEnabled == e.corr, e.id);
        CHECK(m->noiseSubtractionEnabled == e.sub, e.id);
        CHECK(nearF(m->spatialZ, e.z) && nearF(m->spatialSize, e.size), e.id);
        // PAs (103 dB) exige que el rango unificado de SPL sea 30–110 en las 3
        // versiones: 103 debe caber y 30 no.
        CHECK(m->targetSpl >= 30.0f && m->targetSpl <= 110.0f, "SPL dentro de 30–110");
    }

    // Banco de bandas de apoyo por ecu (idéntico en los 6 modos, asignado por
    // zonas: agudos / medios / sub-graves)
    float banks[3][4] = {
        {2500.0f, 5000.0f, 10000.0f, 20000.0f},
        {63.0f, 250.0f, 1000.0f, 4000.0f},
        {20.0f, 41.0f, 62.0f, 83.0f},
    };
    for (const auto& e : expect) {
        const auto* m = byId(e.id);
        if (!m) continue;
        for (int eq = 0; eq < 3; ++eq)
            for (int k = 0; k < 4; ++k)
                CHECK(nearF(m->eqs[eq].support[k].frequencyHz, banks[eq][k], 0.5f),
                      "frecuencia del banco de apoyo");
    }

    // by_id desconocido → NULL (no un índice fuera de rango)
    CHECK(byId("no-existe") == nullptr, "id desconocida → NULL");

    std::printf("MODES (core C): %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
