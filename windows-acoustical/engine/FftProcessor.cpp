// FftProcessor.cpp — delega todo el cálculo al núcleo DSP compartido
// (dsp/acoustical_dsp.c). La API pública (FftProcessor.h) no cambia: el
// proceso de audio sigue recibiendo las mismas magnitudes en dB por bin.
//
// El núcleo precalcula la ventana Hamming, la tabla de bit-reversal y la tabla
// de twiddles UNA sola vez en el constructor (antes se recalculaban los
// cos/sin de cada twiddle en cada frame).
#include "FftProcessor.h"
#include "acoustical_dsp.h"

#include <stdexcept>

namespace acoustical {

FftProcessor::FftProcessor(int size) : size_(size) {
    if (size <= 0 || (size & (size - 1)) != 0)
        throw std::invalid_argument("FFT size must be power of 2");

    core_ = acoustical_fft_create(size);
    if (!core_)
        throw std::runtime_error("acoustical_fft_create failed");

    magnitudeDb_.assign(size / 2, -120.0f);
    binFreqs_.assign(size / 2, 0.0f);
}

FftProcessor::~FftProcessor() {
    if (core_) acoustical_fft_free(core_);
}

const std::vector<float>& FftProcessor::computeMagnitudesDb(const float* input, int sampleRate) {
    acoustical_fft_compute_magnitudes_db(core_, input, sampleRate, magnitudeDb_.data());
    return magnitudeDb_;
}

const std::vector<float>& FftProcessor::binFrequencies(int sampleRate) {
    acoustical_fft_bin_frequencies(core_, sampleRate, binFreqs_.data());
    return binFreqs_;
}

} // namespace acoustical
