// FftProcessor.h — port de FftProcessor.kt: radix-2 Cooley-Tukey, ventana
// Hamming, magnitudes en dB. Tamaños potencia de dos.
//
// El cálculo vive en el núcleo DSP compartido (dsp/acoustical_dsp.h); esta
// clase es un fino envoltorio que mantiene la API pública invariable.
#pragma once

#include "AcousticalParameters.h"
#include "acoustical_dsp.h"

namespace acoustical {

class FftProcessor {
public:
    explicit FftProcessor(int size);
    ~FftProcessor();

    // No se copia: el núcleo C es un puntero único.
    FftProcessor(const FftProcessor&) = delete;
    FftProcessor& operator=(const FftProcessor&) = delete;

    int binCount() const { return size_ / 2; }

    // Devuelve magnitudes en dB por bin (size/2 valores). Interno reutilizable.
    const std::vector<float>& computeMagnitudesDb(const float* input, int sampleRate);

    const std::vector<float>& binFrequencies(int sampleRate);

private:
    int size_;
    acoustical_fft* core_;
    std::vector<float> magnitudeDb_, binFreqs_;
};

} // namespace acoustical
