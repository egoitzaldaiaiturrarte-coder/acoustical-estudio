#include "FftProcessor.h"
#include <cmath>
#include <stdexcept>

namespace acoustical {

FftProcessor::FftProcessor(int size) : size_(size) {
    if (size <= 0 || (size & (size - 1)) != 0)
        throw std::invalid_argument("FFT size must be power of 2");
    real_.assign(size, 0.0f);
    imag_.assign(size, 0.0f);
    window_.assign(size, 0.0f);
    magnitudeDb_.assign(size / 2, -120.0f);
    binFreqs_.assign(size / 2, 0.0f);

    for (int i = 0; i < size; ++i)
        window_[i] = static_cast<float>(0.54 - 0.46 * std::cos(2.0 * 3.14159265358979323846 * i / (size - 1)));

    // Tabla de inversión de bits
    int bits = 0;
    for (int t = size; t > 1; t >>= 1) ++bits;
    bitReverseTable_.assign(size, 0);
    for (int i = 0; i < size; ++i) {
        int rev = 0, x = i;
        for (int j = 0; j < bits; ++j) { rev = (rev << 1) | (x & 1); x >>= 1; }
        bitReverseTable_[i] = rev;
    }
}

const std::vector<float>& FftProcessor::computeMagnitudesDb(const float* input, int sampleRate) {
    const int n = size_;
    for (int i = 0; i < n; ++i) { real_[i] = input[i] * window_[i]; imag_[i] = 0.0f; }

    for (int i = 0; i < n; ++i) {
        const int j = bitReverseTable_[i];
        if (j > i) {
            std::swap(real_[i], real_[j]);
            std::swap(imag_[i], imag_[j]);
        }
    }

    int stageSize = 2;
    while (stageSize <= n) {
        const int halfStage = stageSize / 2;
        const double angleStep = -2.0 * 3.14159265358979323846 / stageSize;
        for (int i = 0; i < halfStage; ++i) {
            const float wReal = static_cast<float>(std::cos(angleStep * i));
            const float wImag = static_cast<float>(std::sin(angleStep * i));
            for (int j = i; j < n; j += stageSize) {
                const int k = j + halfStage;
                const float tReal = wReal * real_[k] - wImag * imag_[k];
                const float tImag = wReal * imag_[k] + wImag * real_[k];
                real_[k] = real_[j] - tReal;
                imag_[k] = imag_[j] - tImag;
                real_[j] = real_[j] + tReal;
                imag_[j] = imag_[j] + tImag;
            }
        }
        stageSize <<= 1;
    }

    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(n);
    const float normFactor = 2.0f / static_cast<float>(n);
    for (int i = 0; i < n / 2; ++i) {
        const float mag = std::sqrt(real_[i] * real_[i] + imag_[i] * imag_[i]) * normFactor;
        magnitudeDb_[i] = mag > 1e-10f ? 20.0f * std::log10(mag) : -120.0f;
        binFreqs_[i] = i * binHz;
    }
    return magnitudeDb_;
}

const std::vector<float>& FftProcessor::binFrequencies(int sampleRate) {
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(size_);
    for (int i = 0; i < size_ / 2; ++i) binFreqs_[i] = i * binHz;
    return binFreqs_;
}

} // namespace acoustical
