#include "SignalGenerator.h"
#include <cmath>

namespace acoustical {

void SignalGenerator::fill(float* out, int count, float sampleRate) {
    // Snapshot de bloque: se leen los atómicos UNA vez al inicio (los escribe
    // la UI desde otro hilo) y todo el bloque se genera en el core con ese
    // conjunto coherente. Sin esto, un cambio de parámetro a mitad de bloque
    // daría muestras con parámetros "torn" (pop audible, saltos de fase).
    //
    // El core (dsp/acoustical_dsp.c) es la única implementación: mismo seno,
    // mismo barrido con fase integrada, mismo rosa de Paul Kellet y mismo
    // PRNG que el fallback de Android. Si el waveform cambia respecto al
    // bloque anterior, el core resetea sus fases/filtro en ESTE hilo (nunca
    // la UI): volver a rosa no deja transiente de nivel.
    const int wave = canonicalWaveform(waveform());
    const float freq = frequency_.load(std::memory_order_acquire);
    const float amp = amplitude_.load(std::memory_order_acquire);
    const float swStart = sweepStartHz_.load(std::memory_order_acquire);
    const float swEnd = sweepEndHz_.load(std::memory_order_acquire);
    const float swSec = sweepSeconds_.load(std::memory_order_acquire);

    // handle_ null solo si el create() del constructor falló (OOM): el
    // bloque se silencia en vez de dereferenciar.
    if (!handle_) {
        for (int i = 0; i < count; ++i) out[i] = 0.0f;
        return;
    }
    acoustical_siggen_fill(handle_, wave, freq, amp, swStart, swEnd, swSec,
                            sampleRate, out, count);
}

std::vector<float> SignalGenerator::makeLogSweep(float startHz, float endHz, float seconds, float sampleRate) {
    // Precomputado estático para la captura de referencia: fase integrada del
    // barrido (misma ley que el generador en vivo, amplitud 0.8).
    std::vector<float> out(static_cast<int>(seconds * sampleRate));
    const double twoPi = 2.0 * 3.14159265358979323846;
    const double ratio = std::log(static_cast<double>(endHz) / startHz) / seconds;
    double phase = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double instFreq = startHz * std::exp(ratio * t);
        phase += twoPi * instFreq / sampleRate;  // fase integrada (sin aliasing)
        out[i] = static_cast<float>(0.8 * std::sin(phase));
    }
    return out;
}

} // namespace acoustical
