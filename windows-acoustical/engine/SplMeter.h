// SplMeter.h — port de SplMeter.kt: medidor de presión sonora con calibración,
// pico e histórico.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <memory>
#include <array>

namespace acoustical {

class SplMeter {
public:
    explicit SplMeter(float calibrationOffset = 120.0f) : calibrationOffset_(calibrationOffset) {}

    float computeSpl(const float* samples, int count) {
        if (count <= 0) return 0.0f;
        double sumSquares = 0.0;
        for (int i = 0; i < count; ++i) sumSquares += double(samples[i]) * samples[i];
        const double rms = std::sqrt(sumSquares / count);
        const double dbfs = rms > 1e-10 ? 20.0 * std::log10(rms) : -120.0;
        const float spl = static_cast<float>(std::clamp(dbfs + calibrationOffset_, 0.0, 200.0));
        if (spl > peakSpl_) peakSpl_ = spl;
        history_[historyIndex_] = spl;
        historyIndex_ = (historyIndex_ + 1) % kHistorySize;
        if (historyCount_ < kHistorySize) ++historyCount_;
        return spl;
    }

    float peakSpl() const { return peakSpl_; }

    float averageSpl() const {
        if (historyCount_ == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < historyCount_; ++i) sum += history_[i];
        return sum / historyCount_;
    }

    void resetPeak() { peakSpl_ = 0.0f; }
    void resetAll() { historyIndex_ = 0; historyCount_ = 0; peakSpl_ = 0.0f; }

private:
    static constexpr int kHistorySize = 100;
    float calibrationOffset_;
    float peakSpl_ = 0.0f;
    std::array<float, kHistorySize> history_{};
    int historyIndex_ = 0;
    int historyCount_ = 0;
};

} // namespace acoustical
