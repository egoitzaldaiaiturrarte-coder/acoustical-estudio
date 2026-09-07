#include "SignalGenerator.h"
#include <cmath>
#include <random>

namespace acoustical {

namespace {
    thread_local std::mt19937 rng{std::random_device{}()};
    thread_local std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
}

void SignalGenerator::fill(float* out, int count, float sampleRate) {
    if (waveform_ == Waveform::Silence) {
        std::fill(out, out + count, 0.0f);
        return;
    }
    const float twoPi = 3.14159265358979323846f;

    for (int i = 0; i < count; ++i) {
        float sample = 0.0f;
        switch (waveform_) {
            case Waveform::Sine:
            case Waveform::BandSine: {
                sample = std::sin(phase_);
                phase_ += twoPi * frequency_ / sampleRate;
                if (phase_ > twoPi) phase_ -= twoPi;
                break;
            }
            case Waveform::LogSweep: {
                // Fase integral del barrido logarítmico: f(t) = f0·(f1/f0)^(t/T)
                const double T = static_cast<double>(sweepSeconds_);
                const double t = sweepPhase_;
                const double ratio = std::log(static_cast<double>(sweepEndHz_) / sweepStartHz_) / T;
                const double instFreq = sweepStartHz_ * std::exp(ratio * t);
                sample = static_cast<float>(std::sin(twoPi * instFreq * t));
                sweepPhase_ += 1.0 / static_cast<double>(sampleRate);
                if (sweepPhase_ >= T) { sweepPhase_ = 0.0; }
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
        out[i] = sample * amplitude_;
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
        out[i] = static_cast<float>(0.8 * std::sin(twoPi * instFreq * t));
    }
    return out;
}

} // namespace acoustical
