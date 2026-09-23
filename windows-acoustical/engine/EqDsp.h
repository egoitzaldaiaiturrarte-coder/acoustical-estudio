// EqDsp.h — banco real de filtros (biquad peaking RBJ) por banda y canal, que
// aplica las ganancias del EQ y de los ecuas dinámicos al audio en tiempo real.
//
// Thread-safe: el writer (hilo del motor / UI) construye un conjunto de
// coeficientes inmutable y lo publica con un std::atomic<std::shared_ptr>; el
// hilo de audio solo lo carga (load) y lo procesa. No hay malloc ni lock en el
// callback de audio, y no hay carrera entre setGains/prepare y process().
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <memory>
#include <array>
#include <atomic>
#include <mutex>

namespace acoustical {

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 1.0f;
};

// Coeficientes de un peaking EQ (RBJ cookbook)
BiquadCoeffs makePeaking(float centerFreqHz, float sampleRate, float gainDb, float q);

// Conjunto inmutable de coeficientes + ganancias que el hilo de audio lee.
struct EqCoeffSet {
    static constexpr int kMaxBands = 124;
    std::array<BiquadCoeffs, kMaxBands> coeffs{};
    std::array<float, kMaxBands> gain{};
    int bands = 0;
};

// Banco de EQ con curva combinada: EQ manual + las tres curvas de los ecuas
// dinámicos (escaladas por su mezclador, ya incluidas en gainsL/R).
class EqDsp {
public:
    static constexpr int kMaxBands = EqCoeffSet::kMaxBands;

    EqDsp();

    // (Writer) Configura el banco. Puede llamarse desde cualquier hilo; el
    // audio no se detiene. Re-crea el snapshot plano y pide reset de estado.
    void prepare(int bands, const float* bandFrequencies, float sampleRate, float q = 1.41f);

    // (Writer) Actualiza las ganancias combinadas por banda. Publica un nuevo
    // snapshot inmutable; el audio lo adopta en el siguiente bloque.
    void setGains(bool /*rightChannel*/, const std::vector<float>& combinedGainsDb);

    // (Audio) Procesa un bloque de audio in-place (canal único).
    void process(float* samples, int numSamples);

private:
    // Construye un snapshot con las ganancias dadas (requiere writerMutex_).
    std::shared_ptr<EqCoeffSet> buildSet(const std::vector<float>& gainsDb) const;

    // Snapshot inmutable publicado de forma atómica (std::atomic_store) y leído
    // con std::atomic_load: portable (GCC/MSVC) y sin lock en el camino de audio.
    std::shared_ptr<EqCoeffSet> current_;

    // === Solo el hilo de audio (sin locks) ===
    std::array<float, kMaxBands> z1_{};
    std::array<float, kMaxBands> z2_{};
    std::array<float, kMaxBands> lastGain_{};  // para evitar pop al (re)activar banda
    std::atomic<bool> stateReset_{false};

    // === Solo el writer (serializado con writerMutex_) ===
    mutable std::mutex writerMutex_;
    int bands_ = 0;
    float sampleRate_ = 48000.0f;
    float q_ = 1.41f;
    std::vector<float> bandFreqs_;
};

} // namespace acoustical
