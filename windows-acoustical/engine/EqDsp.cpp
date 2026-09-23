#include "EqDsp.h"
#include "acoustical_dsp.h"
#include <algorithm>
#include <cmath>

namespace acoustical {

// Los coeficientes del peaking EQ (RBJ cookbook) viven en el núcleo DSP
// compartido (dsp/acoustical_dsp.c), que es la única fuente de verdad y la
// que cubren los tests.
BiquadCoeffs makePeaking(float f0, float fs, float gainDb, float q) {
    acoustical_biquad_coeffs c;
    acoustical_make_peaking(f0, fs, gainDb, q, &c);
    BiquadCoeffs out;
    out.b0 = c.b0; out.b1 = c.b1; out.b2 = c.b2; out.a1 = c.a1; out.a2 = c.a2;
    return out;
}

EqDsp::EqDsp() {
    // Snapshot plano inicial (0 bandas) para que process() nunca vea nullptr.
    std::atomic_store(&current_, std::make_shared<EqCoeffSet>());
}

std::shared_ptr<EqCoeffSet> EqDsp::buildSet(const std::vector<float>& gainsDb) const {
    auto s = std::make_shared<EqCoeffSet>();
    s->bands = bands_;
    for (int i = 0; i < bands_; ++i) {
        const float g = i < static_cast<int>(gainsDb.size()) ? gainsDb[i] : 0.0f;
        s->gain[i] = g;
        s->coeffs[i] = makePeaking(bandFreqs_[i], sampleRate_, g, q_);
    }
    return s;
}

void EqDsp::prepare(int bands, const float* bandFrequencies, float sampleRate, float q) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    bands_ = std::min(bands, kMaxBands);
    sampleRate_ = sampleRate;
    q_ = q;
    bandFreqs_.assign(bandFrequencies, bandFrequencies + bands_);
    std::vector<float> flat(bands_, 0.0f);
    std::atomic_store(&current_, buildSet(flat));
    // El estado de los biquads se pone a cero en el hilo de audio (seguro).
    stateReset_.store(true, std::memory_order_release);
}

void EqDsp::setGains(bool /*rightChannel*/, const std::vector<float>& combinedGainsDb) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    std::atomic_store(&current_, buildSet(combinedGainsDb));
}

void EqDsp::process(float* samples, int numSamples) {
    auto snap = std::atomic_load(&current_);
    if (!snap) return;

    if (stateReset_.exchange(false, std::memory_order_relaxed)) {
        z1_.fill(0.0f);
        z2_.fill(0.0f);
        lastGain_.fill(0.0f);
    }

    const int n = std::min(snap->bands, kMaxBands);
    for (int i = 0; i < n; ++i) {
        const float g = snap->gain[i];
        const bool active = std::abs(g) >= 0.001f;
        const bool wasActive = std::abs(lastGain_[i]) >= 0.001f;
        // (Re)activación de la banda: reinicia el estado para evitar un clic.
        if (active != wasActive) { z1_[i] = 0.0f; z2_[i] = 0.0f; }
        lastGain_[i] = g;
        if (!active) continue;  // banda sin ganancia: sin filtrar

        const BiquadCoeffs& c = snap->coeffs[i];
        float& z1 = z1_[i];
        float& z2 = z2_[i];
        for (int s = 0; s < numSamples; ++s) {
            const float x = samples[s];
            const float y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            samples[s] = y;
        }
    }
}

} // namespace acoustical
