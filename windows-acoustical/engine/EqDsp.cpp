#include "EqDsp.h"
#include <algorithm>
#include <cmath>

namespace acoustical {

BiquadCoeffs makePeaking(float f0, float fs, float gainDb, float q) {
    BiquadCoeffs c;
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * 3.14159265358979323846f * f0 / fs;
    const float cw = std::cos(w0), sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha / A;
    c.b0 = (1.0f + alpha * A) / a0;
    c.b1 = (-2.0f * cw) / a0;
    c.b2 = (1.0f - alpha * A) / a0;
    c.a1 = (-2.0f * cw) / a0;
    c.a2 = (1.0f - alpha / A) / a0;
    return c;
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
