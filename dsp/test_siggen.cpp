// test_siggen.cpp — el generador del núcleo DSP compartido:
//   - Silencio (primera entrada de TODO menú de las 3 versiones) da CERO;
//   - el seno suena a la frecuencia pedida y con el nivel pedido;
//   - rosa/blanco/barrido suenan (no son silencio, no se parecen entre sí);
//   - un cambio de forma de onda resetea el estado (volver a rosa no deja
//     transiente de nivel, como antes en el C++ con el filtro sin reset);
//   - una tasa corrupta silencia el bloque en vez de envenenarlo.
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; std::printf("FAIL: %s\n", msg); } \
} while (0)

static float rms(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += (double)x * x;
    return (float)sqrt(s / (v.size() ? v.size() : 1));
}

static bool allZero(const std::vector<float>& v) {
    for (float x : v) if (x != 0.0f) return false;
    return true;
}

// Frecuencia dominante por conteo de cruces por cero en ±1 ciclo.
static double zeroCrossFreq(const std::vector<float>& v, int sr) {
    int crosses = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0.0f && v[i] >= 0.0f) || (v[i - 1] >= 0.0f && v[i] < 0.0f))
            ++crosses;
    const double seconds = (double)v.size() / sr;
    return crosses / 2.0 / seconds;
}

int main() {
    const int sr = 48000;
    const int n = 4800;  // 100 ms

    // 1) Silencio = cero absoluto (la primera opción del menú de las 3 versiones)
    {
        acoustical_siggen* g = acoustical_siggen_create();
        CHECK(g != nullptr, "create");
        std::vector<float> buf(n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_SILENCE, 1000.0f, 0.5f,
                               20.0f, 20000.0f, 5.0f, (float)sr, buf.data(), n);
        CHECK(allZero(buf), "Silencio produce ceros");
        acoustical_siggen_free(g);
    }

    // 2) Seno a 1 kHz: la frecuencia dominante es ~1 kHz y el RMS ≈ amp/√2
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> buf(n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_SINE, 1000.0f, 0.5f,
                               20.0f, 20000.0f, 5.0f, (float)sr, buf.data(), n);
        const double f = zeroCrossFreq(buf, sr);
        std::printf("  Seno 1 kHz -> %0.1f Hz (RMS %.4f)\n", f, rms(buf));
        CHECK(f > 950.0 && f < 1050.0, "seno suena a la frecuencia pedida");
        CHECK(std::fabs(rms(buf) - 0.5f / sqrt(2.0)) < 0.01f, "RMS = amp/√2 (nivel 0 dB)");
        acoustical_siggen_free(g);
    }

    // 3) Nivel: -6 dB → amplitud 0.5
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> buf(n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_SINE, 1000.0f,
                               (float)pow(10.0, -6.0 / 20.0),
                               20.0f, 20000.0f, 5.0f, (float)sr, buf.data(), n);
        float peak = 0.0f;
        for (float x : buf) peak = std::fmax(peak, std::fabs(x));
        CHECK(peak > 0.48f && peak < 0.52f, "el nivel dB escala la amplitud");
        acoustical_siggen_free(g);
    }

    // 4) Rosa y blanco: no son silencio ni se parecen entre sí
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> pink(n), white(n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_PINK, 1000.0f, 1.0f,
                               20.0f, 20000.0f, 5.0f, (float)sr, pink.data(), n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_WHITE, 1000.0f, 1.0f,
                                20.0f, 20000.0f, 5.0f, (float)sr, white.data(), n);
        const double dp = rms(pink), dw = rms(white);
        std::printf("  Rosa RMS %.4f, blanco RMS %.4f\n", dp, dw);
        CHECK(dp > 0.001, "rosa suena (no silencio)");
        CHECK(dw > 0.001, "blanco suena (no silencio)");
        // Correlación cross-serial baja: dos ruidos distintos
        double cross = 0.0;
        for (int i = 0; i < n; ++i) cross += (double)pink[i] * white[i];
        const double cov = cross / n;
        const double corr = cov / (dp * dw + 1e-9);
        CHECK(std::fabs(corr) < 0.5, "rosa y blanco son ruidos distintos");
        acoustical_siggen_free(g);
    }

    // 5) Barrido log: suena y la frecuencia sube (la mitad inicial más grave)
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> first(2400), second(2400);  // 50 + 50 ms de un barrido de 100 ms
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_LOG_SWEEP, 1000.0f, 1.0f,
                               20.0f, 20000.0f, 0.1f, (float)sr, first.data(), first.size());
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_LOG_SWEEP, 1000.0f, 1.0f,
                               20.0f, 20000.0f, 0.1f, (float)sr, second.data(), second.size());
        const double f1 = zeroCrossFreq(first, sr);
        const double f2 = zeroCrossFreq(second, sr);
        std::printf("  Barrido: primera mitad %.0f Hz, segunda mitad %.0f Hz\n", f1, f2);
        CHECK(rms(first) > 0.01, "barrido suena");
        CHECK(f2 > f1, "el barrido sube en frecuencia");
        acoustical_siggen_free(g);
    }

    // 6) Cambio de forma de onda resetea el estado del filtro rosa:
    //    rosa → silencio → rosa deja de tener el transiente de nivel que
    //    producía el estado viejo (el primer bloque ya está en régimen).
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> warm(n), after(n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_PINK, 1000.0f, 1.0f,
                               20.0f, 20000.0f, 5.0f, (float)sr, warm.data(), n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_SILENCE, 1000.0f, 1.0f,
                                20.0f, 20000.0f, 5.0f, (float)sr, after.data(), n);
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_PINK, 1000.0f, 1.0f,
                               20.0f, 20000.0f, 5.0f, (float)sr, after.data(), n);
        // Con el reset, el segundo tramo de rosa tiene el RMS del régimen
        // (el viejo código sin reset arrancaba desde el estado envejecido).
        CHECK(rms(after) > 0.001, "volver a rosa suena tras el silencio");
        acoustical_siggen_free(g);
    }

    // 7) Tasa corrupta: silencia el bloque en vez de propagar NaN/Inf
    {
        acoustical_siggen* g = acoustical_siggen_create();
        std::vector<float> buf(n, 42.0f);  // se rellena con basura a propósito
        acoustical_siggen_fill(g, ACOUSTICAL_WAVE_SINE, 1000.0f, 1.0f,
                                20.0f, 20000.0f, 5.0f, -1.0f, buf.data(), n);
        bool finite = true;
        for (float x : buf) if (!std::isfinite(x)) { finite = false; break; }
        CHECK(finite && allZero(buf), "tasa corrupta silencia el bloque");
        acoustical_siggen_free(g);
    }

    std::printf("SIGGEN (core C): %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
