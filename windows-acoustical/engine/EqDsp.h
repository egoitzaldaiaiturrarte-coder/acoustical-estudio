// EqDsp.h — banco real de filtros (biquad peaking RBJ) por banda y canal, que
// aplica las ganancias del EQ y de los ecuas dinámicos al audio en tiempo real.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <memory>
#include <array>
#include <atomic>

namespace acoustical {

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

class BiquadState {
public:
    float process(float x) {
        const float y = coeffs.b0 * x + z1_;
        z1_ = coeffs.b1 * x - coeffs.a1 * y + z2_;
        z2_ = coeffs.b2 * x - coeffs.a2 * y;
        return y;
    }
    BiquadCoeffs coeffs;
private:
    float z1_ = 0.0f, z2_ = 0.0f;
};

// Coeficientes de un peaking EQ (RBJ cookbook)
BiquadCoeffs makePeaking(float centerFreqHz, float sampleRate, float gainDb, float q);

// Banco de EQ con curva combinada: EQ manual + las tres curvas de los ecuas
// dinámicos (escaladas por su mezclador, ya incluidas en gainsL/R).
class EqDsp {
public:
    static constexpr int kMaxBands = 124;

    void prepare(int bands, const float* bandFrequencies, float sampleRate, float q = 1.41f);

    // Actualiza las ganancias combinadas por banda (L o R). Llamado desde la
    // UI/hilo de análisis; el audio lee el snapshot atómico.
    void setGains(bool rightChannel, const std::vector<float>& combinedGainsDb);

    // Procesa un bloque de audio in-place (canal único)
    void process(float* samples, int numSamples);

private:
    void rebuildCoefficients(bool rightChannel);

    int bands_ = 0;
    float sampleRate_ = 48000.0f;
    float q_ = 1.41f;
    std::vector<float> bandFreqs_;

    // Doble buffer de ganancias con puntero atómico (libre de bloqueos)
    struct GainSnapshot {
        std::vector<float> gains;
    };
    std::unique_ptr<GainSnapshot> gainSets_[2];
    std::atomic<int> activeGainSet_{0};
    std::atomic<bool> dirty_{false};
    std::vector<float> lastApplied_;

    std::array<BiquadState, kMaxBands> filters_{};
    std::array<BiquadCoeffs, kMaxBands> cachedCoeffs_{};
};

} // namespace acoustical
