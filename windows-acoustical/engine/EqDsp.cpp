#include "EqDsp.h"
#include "acoustical_dsp.h"
#include <algorithm>
#include <cmath>

namespace acoustical {

// Los coeficientes del peaking EQ (RBJ cookbook) viven en el núcleo DSP
// compartido (dsp/acoustical_dsp.c), que es la única fuente de verdad y la
// que cubren los tests. El paso por muestra (BiquadState::process) se queda
// aquí, inline, para no añadir una llamada en el hot path de audio.
BiquadCoeffs makePeaking(float f0, float fs, float gainDb, float q) {
    acoustical_biquad_coeffs c;
    acoustical_make_peaking(f0, fs, gainDb, q, &c);
    BiquadCoeffs out;
    out.b0 = c.b0; out.b1 = c.b1; out.b2 = c.b2; out.a1 = c.a1; out.a2 = c.a2;
    return out;
}

void EqDsp::prepare(int bands, const float* bandFrequencies, float sampleRate, float q) {
    bands_ = std::min(bands, kMaxBands);
    sampleRate_ = sampleRate;
    q_ = q;
    bandFreqs_.assign(bandFrequencies, bandFrequencies + bands_);
    gainSets_[0] = std::make_unique<GainSnapshot>();
    gainSets_[1] = std::make_unique<GainSnapshot>();
    gainSets_[0]->gains.assign(bands_, 0.0f);
    gainSets_[1]->gains.assign(bands_, 0.0f);
    lastApplied_.assign(bands_, 0.0f);
    activeGainSet_.store(0, std::memory_order_relaxed);
    dirty_.store(true, std::memory_order_relaxed);
}

void EqDsp::setGains(bool rightChannel, const std::vector<float>& combinedGainsDb) {
    const int slot = rightChannel ? 1 : 0;
    if (!gainSets_[slot]) return;
    const int n = std::min<int>(bands_, static_cast<int>(combinedGainsDb.size()));
    gainSets_[slot]->gains.resize(n);
    for (int i = 0; i < n; ++i) gainSets_[slot]->gains[i] = combinedGainsDb[i];
    dirty_.store(true, std::memory_order_release);
}

void EqDsp::rebuildCoefficients(bool rightChannel) {
    const int slot = rightChannel ? 1 : 0;
    if (!gainSets_[slot]) return;
    const auto& gains = gainSets_[slot]->gains;
    for (int i = 0; i < bands_; ++i) {
        const float g = i < static_cast<int>(gains.size()) ? gains[i] : 0.0f;
        if (std::abs(g - lastApplied_[i]) > 0.01f || g == 0.0f) {
            cachedCoeffs_[i] = makePeaking(bandFreqs_[i], sampleRate_, g, q_);
            lastApplied_[i] = g;
        }
    }
    dirty_.store(false, std::memory_order_relaxed);
}

void EqDsp::process(float* samples, int numSamples) {
    const int slot = activeGainSet_.load(std::memory_order_relaxed);
    if (dirty_.load(std::memory_order_acquire)) {
        rebuildCoefficients(slot == 1);
    }
    for (int i = 0; i < bands_; ++i) {
        if (std::abs(lastApplied_[i]) < 0.001f) continue;  // banda sin ganancia: sin filtrar
        BiquadState& f = filters_[i];
        f.coeffs = cachedCoeffs_[i];
        for (int s = 0; s < numSamples; ++s) samples[s] = f.process(samples[s]);
    }
}

} // namespace acoustical
