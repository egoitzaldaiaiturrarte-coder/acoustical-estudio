// test_spatial.cpp — la compensación espacial (ganancia + retardo) del núcleo
// compartido: la MISMA ley que aplican las tres versiones (app Windows, VST y
// Android), así el "cerca y pequeño" suena igual dondequiera que se replice.
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; std::printf("FAIL: %s\n", msg); } \
} while (0)

static bool nearF(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) <= eps;
}

int main() {
    float gain, delay;

    // Centro, media distancia, tamaño medio: solo la ley de distancia.
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.5f, 0.5f, &gain, &delay);
        const float expectGain = 20.0f * log10f(7.5f);
        const float expectDelay = (7.5f / 343.0f) * 1000.0f;
        std::printf("  z=0.5: +%.2f dB, +%.2f ms\n", gain, delay);
        CHECK(nearF(gain, expectGain), "ganancia = 20·log10(distancia)");
        CHECK(nearF(delay, expectDelay, 0.1f), "retardo = d/343·1000");
    }

    // Cerca (z=0): sin ganancia ni retardo (0 m < 0.5 → 0 dB)
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.0f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 0.0f) && nearF(delay, 0.0f), "cerca = 0 dB / 0 ms");
    }

    // Lejos y grande (z=0.9, size=1): la distancia manda a ganar, el tamaño
    // grande resta 3 dB (el elemento rinde más).
    {
        const float z = 0.9f;
        acoustical_spatial_compensate(0.0f, 0.0f, z, 1.0f, &gain, &delay);
        const float d = z * 15.0f;
        const float expect = 20.0f * log10f(d) + (0.5f - 1.0f) * 6.0f;
        std::printf("  z=0.9, grande: +%.2f dB\n", gain);
        CHECK(nearF(gain, expect), "lejos + grande = distancia − 3 dB");
        CHECK(nearF(delay, (d / 343.0f) * 1000.0f, 0.1f), "retardo de 13.5 m");
    }

    // Lateral (x=±1): +1.5 dB de ganancia off-axis
    {
        acoustical_spatial_compensate(1.0f, 0.0f, 0.0f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x=+1 → +1.5 dB");
        acoustical_spatial_compensate(-1.0f, 0.0f, 0.0f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x=−1 → +1.5 dB (simétrico)");
    }

    // Pequeño (size=0): +3 dB (rinde menos)
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.0f, 0.0f, &gain, &delay);
        CHECK(nearF(gain, 3.0f), "size=0 → +3 dB");
    }

    // Entradas corruptas (NaN/Inf) no envenenan: 0 dB / 0 ms
    {
        acoustical_spatial_compensate(NAN, 0.0f, 0.5f, 0.5f, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "NaN en x → 0/0");
        acoustical_spatial_compensate(0.0f, 0.0f, INFINITY, 0.5f, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "Inf en z → 0/0");
    }

    // Fuera de rango: se clampa (x=2 → 1; z=5 → 1)
    {
        acoustical_spatial_compensate(2.0f, 0.0f, 0.0f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x>1 se clampa a 1");
        acoustical_spatial_compensate(0.0f, 0.0f, 5.0f, 0.5f, &gain, &delay);
        const float d = 15.0f;
        CHECK(nearF(delay, (d / 343.0f) * 1000.0f, 0.1f), "z>1 se clampa a 1 (15 m)");
    }

    std::printf("SPATIAL (core C): %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
