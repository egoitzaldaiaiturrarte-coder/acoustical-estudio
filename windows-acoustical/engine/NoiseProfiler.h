// NoiseProfiler.h — port de NoiseProfiler.kt: perfil de ruido de fondo con
// resta espectral por compuerta.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <memory>

namespace acoustical {

class NoiseProfiler {
public:
    NoiseProfiler(int binCount, int maxCaptureFrames = 50, float gateRange = 6.0f)
        : binCount_(binCount), maxCaptureFrames_(maxCaptureFrames), gateRange_(gateRange) {
        accumulated_.assign(binCount, 0.0f);
    }

    const std::vector<float>* profile() const { return noiseProfile_.get(); }
    bool hasProfile() const { return noiseProfile_ != nullptr; }

    void startCapture() {
        std::fill(accumulated_.begin(), accumulated_.end(), 0.0f);
        capturedFrames_ = 0;
        noiseProfile_.reset();
        capturing_ = true;
    }

    // Devuelve true cuando la captura está completa
    bool feedFrame(const std::vector<float>& magsDb) {
        if (!capturing_ || static_cast<int>(magsDb.size()) != binCount_) return false;
        for (int i = 0; i < binCount_; ++i) accumulated_[i] += magsDb[i];
        if (++capturedFrames_ >= maxCaptureFrames_) { finishCapture(); return true; }
        return false;
    }

    void cancelCapture() {
        capturing_ = false;
        std::fill(accumulated_.begin(), accumulated_.end(), 0.0f);
        capturedFrames_ = 0;
    }

    bool isCapturing() const { return capturing_; }

    float captureProgress() const {
        return maxCaptureFrames_ == 0 ? 0.0f
            : std::clamp(static_cast<float>(capturedFrames_) / maxCaptureFrames_, 0.0f, 1.0f);
    }

    // Resta por compuerta: medido <= ruido → ruido; zona de transición proporcional
    std::vector<float> subtractNoise(const std::vector<float>& magsDb) const {
        if (!noiseProfile_ || noiseProfile_->size() != magsDb.size()) return magsDb;
        std::vector<float> result(magsDb.size());
        for (size_t i = 0; i < magsDb.size(); ++i) {
            const float measured = magsDb[i];
            const float noise = (*noiseProfile_)[i];
            if (measured <= noise) {
                result[i] = noise;
            } else if (measured < noise + gateRange_) {
                const float ratio = (measured - noise) / gateRange_;
                const float subtraction = noise * (1.0f - ratio);
                result[i] = std::max(noise, measured - subtraction);
            } else {
                result[i] = measured;
            }
        }
        return result;
    }

    void clearProfile() { noiseProfile_.reset(); }

private:
    void finishCapture() {
        auto profile = std::make_unique<std::vector<float>>(binCount_);
        for (int i = 0; i < binCount_; ++i) (*profile)[i] = accumulated_[i] / capturedFrames_;
        noiseProfile_ = std::move(profile);
        capturing_ = false;
    }

    int binCount_;
    int maxCaptureFrames_;
    float gateRange_;
    std::vector<float> accumulated_;
    int capturedFrames_ = 0;
    std::unique_ptr<std::vector<float>> noiseProfile_;
    bool capturing_ = false;
};

} // namespace acoustical
