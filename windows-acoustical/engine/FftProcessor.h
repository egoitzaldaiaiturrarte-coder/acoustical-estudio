// FftProcessor.h — port de FftProcessor.kt: radix-2 Cooley-Tukey, ventana
// Hamming, magnitudes en dB. Tamaños potencia de dos (512–8192).
#pragma once

#include "AcousticalParameters.h"

namespace acoustical {

class FftProcessor {
public:
    explicit FftProcessor(int size);

    int binCount() const { return size_ / 2; }

    // Devuelve magnitudes en dB por bin (size/2 valores). Interno reutilizable.
    const std::vector<float>& computeMagnitudesDb(const float* input, int sampleRate);

    const std::vector<float>& binFrequencies(int sampleRate);

private:
    int size_;
    std::vector<float> real_, imag_, window_;
    std::vector<int> bitReverseTable_;
    std::vector<float> magnitudeDb_, binFreqs_;
};

} // namespace acoustical
