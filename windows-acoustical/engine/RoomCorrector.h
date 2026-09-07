// RoomCorrector.h — port de RoomCorrector.kt: agregación de bins en bandas
// logarítmicas, corrección rápida de dos bandas (ranking round-robin) y
// suavizado continuo hacia objetivos.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <chrono>
#include "StandardFrequencies.h"

namespace acoustical {

class RoomCorrector {
public:
    RoomCorrector(const std::vector<float>& bandFrequencies, int sampleRate, int fftBinCount,
                  float maxGainDb, float smoothingFactor, float noiseFloorDb)
        : bandFrequencies_(bandFrequencies), sampleRate_(sampleRate), fftBinCount_(fftBinCount),
          maxGainDb_(maxGainDb), smoothingFactor_(smoothingFactor), noiseFloorDb_(noiseFloorDb),
          bandCount_(static_cast<int>(bandFrequencies.size())) {
        targetGains_.assign(bandCount_, 0.0f);
        currentBandLevels_.assign(bandCount_, noiseFloorDb);
        bandEdgeRatio_ = bandCount_ > 40 ? std::pow(2.0, 1.0 / 12.0) : std::pow(2.0, 1.0 / 6.0);
    }

    // Agrega los bins del FFT en bandas perceptuales (media de bins dentro de los bordes)
    std::vector<float> aggregateBands(const std::vector<float>& magnitudesDb,
                                      const std::vector<float>& binFrequencies) {
        std::vector<float> bandLevels(bandCount_);
        for (int b = 0; b < bandCount_; ++b) {
            const float center = bandFrequencies_[b];
            const float lower = static_cast<float>(center / bandEdgeRatio_);
            const float upper = static_cast<float>(center * bandEdgeRatio_);
            double sum = 0.0;
            int count = 0;
            for (size_t i = 0; i < binFrequencies.size(); ++i) {
                const float freq = binFrequencies[i];
                if (freq > upper) break;
                if (freq >= lower && freq <= upper && i < magnitudesDb.size() &&
                    magnitudesDb[i] > noiseFloorDb_) {
                    sum += magnitudesDb[i];
                    ++count;
                }
            }
            bandLevels[b] = count > 0 ? static_cast<float>(sum / count) : noiseFloorDb_;
        }
        currentBandLevels_ = bandLevels;
        return bandLevels;
    }

    // Corrección rápida: cada 500 ms corta la banda más alta del ranking y sube
    // la más baja (round-robin); todas las bandas se suavizan en cada fotograma.
    std::vector<EqBand> computeCorrections(const std::vector<float>& referenceLevels,
                                           const std::vector<float>& measuredLevels,
                                           const std::vector<EqBand>& currentBands) {
        if (currentBands.empty()) return currentBands;
        if (static_cast<int>(targetGains_.size()) != bandCount_) targetGains_.assign(bandCount_, 0.0f);

        const long long now = nowMs();
        if (now - lastCorrectionMs_ >= kTwoBandPeriodMs) {
            lastCorrectionMs_ = now;

            std::vector<int> active;
            for (int i = 0; i < bandCount_; ++i)
                if (i < static_cast<int>(measuredLevels.size()) &&
                    measuredLevels[i] > noiseFloorDb_ + kActiveMarginDb)
                    active.push_back(i);

            if (active.size() >= 2) {
                bool hasRef = static_cast<int>(referenceLevels.size()) == bandCount_;
                if (hasRef) {
                    hasRef = false;
                    for (float v : referenceLevels) if (v != 0.0f) { hasRef = true; break; }
                }
                float mean = 0.0f;
                for (int i : active) mean += measuredLevels[i];
                mean /= static_cast<float>(active.size());

                std::sort(active.begin(), active.end(),
                          [&](int a, int b) { return measuredLevels[a] > measuredLevels[b]; });
                const int rank = std::min(rankCursor_, static_cast<int>(active.size()) - 1);
                const int cutIdx = active[rank];
                const int boostIdx = active[static_cast<int>(active.size()) - 1 - rank];

                if (cutIdx != boostIdx) {
                    const float cutDeviation = hasRef
                        ? measuredLevels[cutIdx] - referenceLevels[cutIdx]
                        : measuredLevels[cutIdx] - mean;
                    const float boostDeviation = hasRef
                        ? measuredLevels[boostIdx] - referenceLevels[boostIdx]
                        : measuredLevels[boostIdx] - mean;
                    // Solo corta lo que supera el punto neutro y sube lo que está por debajo
                    if (cutDeviation > 0.0f)
                        targetGains_[cutIdx] = std::clamp(targetGains_[cutIdx] - cutDeviation, -maxGainDb_, maxGainDb_);
                    if (boostDeviation < 0.0f)
                        targetGains_[boostIdx] = std::clamp(targetGains_[boostIdx] - boostDeviation, -maxGainDb_, maxGainDb_);
                }
                if (++rankCursor_ >= static_cast<int>(active.size())) rankCursor_ = 0;
            }
        }

        // Suavizado continuo hacia objetivos en cada fotograma
        std::vector<EqBand> out = currentBands;
        for (size_t i = 0; i < out.size() && i < targetGains_.size(); ++i) {
            const float target = targetGains_[i];
            out[i].targetGainDb = target;
            out[i].gainDb += (target - out[i].gainDb) * smoothingFactor_;
        }
        return out;
    }

    // Aplica ganancias a un espectro (salida corregida simulada)
    static SpectrumFrame applyGainsToSpectrum(const SpectrumFrame& spectrum,
                                              const std::vector<EqBand>& bands) {
        SpectrumFrame corrected = spectrum;
        corrected.magnitudesDb = spectrum.magnitudesDb;
        for (const auto& band : bands) {
            const double bandwidth = band.centerFreq / band.q;
            const double limit = std::log10(bandwidth / band.centerFreq + 1.0);
            for (size_t i = 0; i < spectrum.frequencies.size(); ++i) {
                const double distance = std::abs(std::log10(spectrum.frequencies[i] / band.centerFreq));
                if (distance < limit) {
                    const float taper = static_cast<float>(1.0 - distance / limit);
                    corrected.magnitudesDb[i] += band.gainDb * taper;
                }
            }
        }
        return corrected;
    }

    // Intensidad de corrección 0–1 normalizada a lo que puede mover el corrector
    static float correctionIntensity(const std::vector<EqBand>& bands, float maxGainDb) {
        if (bands.empty()) return 0.0f;
        double total = 0.0;
        for (const auto& b : bands) total += std::abs(b.gainDb);
        const float maxPossible = std::max(2.0f * maxGainDb, 0.5f);
        return std::clamp(static_cast<float>(total) / maxPossible, 0.0f, 1.0f);
    }

    const std::vector<float>& currentBandLevels() const { return currentBandLevels_; }

    void reset() {
        targetGains_.assign(bandCount_, 0.0f);
        lastCorrectionMs_ = 0;
        rankCursor_ = 0;
    }

private:
    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static constexpr long long kTwoBandPeriodMs = 500;
    static constexpr float kActiveMarginDb = 3.0f;

    const std::vector<float>& bandFrequencies_;
    int sampleRate_;
    int fftBinCount_;
    float maxGainDb_;
    float smoothingFactor_;
    float noiseFloorDb_;
    int bandCount_;
    double bandEdgeRatio_ = std::pow(2.0, 1.0 / 6.0);

    std::vector<float> targetGains_;
    std::vector<float> currentBandLevels_;
    long long lastCorrectionMs_ = 0;
    int rankCursor_ = 0;
};

} // namespace acoustical
