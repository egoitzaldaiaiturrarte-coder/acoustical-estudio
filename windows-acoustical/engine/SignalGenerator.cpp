#include "SignalGenerator.h"
#include <cmath>
#include <random>

namespace acoustical {

namespace {
    // rng/uniform son thread_local: cada hilo de audio tiene su propio estado
    // (el de la UI, si alguna vez genera, no se cruza con el del audio).
    thread_local std::mt19937 rng{std::random_device{}()};
    thread_local std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
}

void SignalGenerator::fill(float* out, int count, float sampleRate) {
    // Snapshot de bloque: se leen los atómicos UNA vez al inicio (los escribe
    // la UI desde otro hilo) y todo el bloque se genera con ese conjunto
    // coherente. Sin esto, un cambio de parámetro a mitad de bloque daría
    // muestras con parámetros "torn" (pop audible, saltos de fase).
    const int wave = waveform_.load(std::memory_order_acquire);
    const float freq = frequency_.load(std::memory_order_acquire);
    const float amp = amplitude_.load(std::memory_order_acquire);
    const float swStart = sweepStartHz_.load(std::memory_order_acquire);
    const float swEnd = sweepEndHz_.load(std::memory_order_acquire);
    const float swSec = sweepSeconds_.load(std::memory_order_acquire);

    // Cambio de forma de onda (vs el snapshot del bloque anterior): se
    // resetean las fases Y el estado del filtro rosa. Antes el reset lo hacía
    // el setter (en el hilo de la UI, escribiendo estado compartido) y el
    // filtro rosa no se reseteaba nunca: conservar el estado viejo al volver
    // a rosa dejaba un transiente de nivel (discontinuidad).
    if (wave != prevWaveform_) {
        prevWaveform_ = wave;
        phase_ = 0.0;
        sweepPhase_ = 0.0;
        sweepIntPhase_ = 0.0;
        pinkB0_ = 0.0f; pinkB1_ = 0.0f; pinkB2_ = 0.0f; pinkB3_ = 0.0f;
        pinkB4_ = 0.0f; pinkB5_ = 0.0f; pinkB6_ = 0.0f;
    }

    if (wave == static_cast<int>(Waveform::Silence)) {
        std::fill(out, out + count, 0.0f);
        return;
    }
    // sampleRate corrupto (<= 0 / no finito) daría incrementos de fase
    // Inf/NaN que envenenarían el buffer de salida: se silencia el bloque.
    if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
        std::fill(out, out + count, 0.0f);
        return;
    }
    const Waveform w = static_cast<Waveform>(wave);
    const double twoPi = 2.0 * 3.14159265358979323846;

    for (int i = 0; i < count; ++i) {
        float sample = 0.0f;
        switch (w) {
            case Waveform::Sine:
            case Waveform::BandSine: {
                sample = static_cast<float>(std::sin(phase_));
                phase_ += twoPi * freq / sampleRate;
                if (phase_ >= twoPi) phase_ -= twoPi;
                break;
            }
            case Waveform::LogSweep: {
                // Fase integrada del barrido logarítmico: f(t) = f0·(f1/f0)^(t/T).
                // Se integra muestra a muestra (phase += 2π·f(t)/fs) para que la
                // frecuencia instantánea sea exactamente f(t), sin aliasing.
                const double T = static_cast<double>(swSec > 0.0f ? swSec : 5.0f);
                const double t = sweepPhase_;
                const double ratio = std::log(static_cast<double>(swEnd) / static_cast<double>(swStart)) / T;
                const double instFreq = swStart * std::exp(ratio * t);
                sweepIntPhase_ += twoPi * instFreq / sampleRate;
                sample = static_cast<float>(std::sin(sweepIntPhase_));
                sweepPhase_ += 1.0 / static_cast<double>(sampleRate);
                if (sweepPhase_ >= T) { sweepPhase_ = 0.0; sweepIntPhase_ = 0.0; }
                break;
            }
            case Waveform::WhiteNoise:
                sample = uniform(rng);
                break;
            case Waveform::PinkNoise: {
                const float white = uniform(rng);
                // Filtro Paul Kellet (economical) — -3 dB/octava
                pinkB0_ = 0.99886f * pinkB0_ + white * 0.0555179f;
                pinkB1_ = 0.99332f * pinkB1_ + white * 0.0750759f;
                pinkB2_ = 0.96900f * pinkB2_ + white * 0.1538520f;
                pinkB3_ = 0.86650f * pinkB3_ + white * 0.3104856f;
                pinkB4_ = 0.55000f * pinkB4_ + white * 0.5329522f;
                pinkB5_ = -0.7616f * pinkB5_ - white * 0.0168980f;
                sample = (pinkB0_ + pinkB1_ + pinkB2_ + pinkB3_ + pinkB4_ + pinkB5_ + pinkB6_ +
                          white * 0.5362f) * 0.11f;
                pinkB6_ = white * 0.115926f;
                break;
            }
            default: break;
        }
        out[i] = sample * amp;
    }
}

std::vector<float> SignalGenerator::makeLogSweep(float startHz, float endHz, float seconds, float sampleRate) {
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
