// test_spatial.cpp — la compensación espacial (ganancia + retardo) del núcleo
// compartido: la MISMA ley que aplican las tres versiones (app Windows, VST y
// Android), así el "cerca y pequeño" suena igual dondequiera que se replice.
//
// v1.10.1: la ley es RELATIVA a la posición neutra (centro, z = 0.5 = 7.5 m,
// tamaño medio), que es la posición por defecto de las tres versiones y da
// exactamente 0 dB / 0 ms. Con la ley absoluta anterior (referencia 1 m),
// abrir la app o insertar el VST subía la salida +17.5 dB y la retrasaba
// 21.9 ms sin tocar nada.
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

    // Neutro (el valor por defecto de la app, el VST y el móvil): NADA.
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.5f, 0.5f, &gain, &delay);
        std::printf("  neutro (z=0.5): %+.2f dB, %.2f ms\n", gain, delay);
        CHECK(gain == 0.0f, "neutro = 0 dB exactos (no toca la salida)");
        CHECK(delay == 0.0f, "neutro = 0 ms exactos (no añade latencia)");
    }

    // y no participa en la ley (solo se guarda con la posición)
    {
        acoustical_spatial_compensate(0.0f, 0.7f, 0.5f, 0.5f, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "y no cambia la compensación");
    }

    // Lejos (z=1, 15 m = el doble que el neutro): +6 dB y el retardo de
    // los 7.5 m de más.
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 1.0f, 0.5f, &gain, &delay);
        std::printf("  lejos (z=1): %+.2f dB, %.2f ms\n", gain, delay);
        CHECK(nearF(gain, 20.0f * log10f(2.0f)), "doble de distancia = +6 dB");
        CHECK(nearF(delay, (7.5f / 343.0f) * 1000.0f, 0.1f), "retardo de los 7.5 m de más");
    }

    // Cerca (z=0): la distancia se acota a 1 m (campo cercano) → −17.5 dB y
    // sin retardo (un retardo negativo no existe).
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.0f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 20.0f * log10f(1.0f / 7.5f)), "cerca = 1 m frente a 7.5 m");
        CHECK(delay == 0.0f, "más cerca que el neutro = 0 ms");
    }

    // Continuidad: la ley vieja saltaba 6 dB al cruzar 0.5 m. Barrido de z
    // fino: dos pasos consecutivos nunca difieren más de 1 dB.
    {
        float prev = 0.0f;
        acoustical_spatial_compensate(0.0f, 0.0f, 0.0f, 0.5f, &prev, &delay);
        bool smooth = true;
        for (int i = 1; i <= 1000; ++i) {
            float g = 0.0f;
            acoustical_spatial_compensate(0.0f, 0.0f, i / 1000.0f, 0.5f, &g, &delay);
            if (std::fabs(g - prev) > 1.0f) smooth = false;
            prev = g;
        }
        CHECK(smooth, "la ganancia es continua en z (sin saltos de 6 dB)");
    }

    // Lejos y grande (z=0.9, size=1): la distancia manda a ganar, el tamaño
    // grande resta 3 dB (el elemento rinde más).
    {
        const float z = 0.9f;
        acoustical_spatial_compensate(0.0f, 0.0f, z, 1.0f, &gain, &delay);
        const float d = z * 15.0f;
        const float expect = 20.0f * log10f(d / 7.5f) + (0.5f - 1.0f) * 6.0f;
        std::printf("  z=0.9, grande: %+.2f dB\n", gain);
        CHECK(nearF(gain, expect), "lejos + grande = distancia relativa − 3 dB");
        CHECK(nearF(delay, ((d - 7.5f) / 343.0f) * 1000.0f, 0.1f), "retardo de los 6 m de más");
    }

    // Lateral (x=±1): +1.5 dB de ganancia off-axis, simétrico
    {
        acoustical_spatial_compensate(1.0f, 0.0f, 0.5f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x=+1 → +1.5 dB");
        acoustical_spatial_compensate(-1.0f, 0.0f, 0.5f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x=−1 → +1.5 dB (simétrico)");
    }

    // Tamaño: pequeño (size=0) +3 dB, grande (size=1) −3 dB
    {
        acoustical_spatial_compensate(0.0f, 0.0f, 0.5f, 0.0f, &gain, &delay);
        CHECK(nearF(gain, 3.0f), "size=0 → +3 dB");
        acoustical_spatial_compensate(0.0f, 0.0f, 0.5f, 1.0f, &gain, &delay);
        CHECK(nearF(gain, -3.0f), "size=1 → −3 dB");
    }

    // Cotas de toda la ley: nunca más de +10.5 dB ni menos de −20.5 dB, ni
    // más de 21.9 ms (lo que la cadena de salida en vivo puede recibir).
    {
        bool bounded = true;
        for (int ix = -2; ix <= 2; ++ix)
            for (int iz = 0; iz <= 10; ++iz)
                for (int is = 0; is <= 4; ++is) {
                    acoustical_spatial_compensate(ix * 0.5f, 0.0f, iz * 0.1f, is * 0.25f, &gain, &delay);
                    if (gain > 10.6f || gain < -20.6f || delay < 0.0f || delay > 21.9f + 0.05f)
                        bounded = false;
                }
        CHECK(bounded, "ganancia en [−20.5, +10.5] dB y retardo en [0, 21.9] ms");
    }

    // Los modos del catálogo: ninguno sube la salida más de +6 dB al
    // aplicarse (antes "Escenario 3" la subía +20 dB de golpe).
    {
        const acoustical_mode_profile* modes = acoustical_mode_profiles();
        bool sane = true;
        for (int i = 0; i < ACOUSTICAL_MODE_COUNT; ++i) {
            const auto& m = modes[i];
            acoustical_spatial_compensate(m.spatialX, m.spatialY, m.spatialZ, m.spatialSize, &gain, &delay);
            std::printf("  modo %-16s %+6.2f dB  %5.2f ms\n", m.id, gain, delay);
            if (gain > 6.0f) sane = false;
        }
        CHECK(sane, "ningún modo del catálogo sube la salida más de +6 dB");
    }

    // Entradas corruptas (NaN/Inf) no envenenan: 0 dB / 0 ms
    {
        acoustical_spatial_compensate(NAN, 0.0f, 0.5f, 0.5f, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "NaN en x → 0/0");
        acoustical_spatial_compensate(0.0f, 0.0f, INFINITY, 0.5f, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "Inf en z → 0/0");
        acoustical_spatial_compensate(0.0f, 0.0f, 0.5f, NAN, &gain, &delay);
        CHECK(gain == 0.0f && delay == 0.0f, "NaN en size → 0/0");
    }

    // Fuera de rango: se clampa (x=2 → 1; z=5 → 1)
    {
        acoustical_spatial_compensate(2.0f, 0.0f, 0.5f, 0.5f, &gain, &delay);
        CHECK(nearF(gain, 1.5f), "x>1 se clampa a 1");
        acoustical_spatial_compensate(0.0f, 0.0f, 5.0f, 0.5f, &gain, &delay);
        CHECK(nearF(delay, (7.5f / 343.0f) * 1000.0f, 0.1f), "z>1 se clampa a 1 (15 m)");
    }

    // Salidas nulas: no se escribe nada (no crashea)
    acoustical_spatial_compensate(0.0f, 0.0f, 1.0f, 0.5f, nullptr, nullptr);

    std::printf("SPATIAL (core C): %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
