// SignalGenerator.h — port ampliado del generador de señales: seno por banda,
// barrido logarítmico, ruido rosa y blanco, con nivel y fundido de entrada/salida.
#pragma once

#include "AcousticalParameters.h"
#include <vector>

namespace acoustical {

class SignalGenerator {
public:
    enum class Waveform { Sine, BandSine, LogSweep, PinkNoise, WhiteNoise, Silence };

    void setWaveform(Waveform w) { waveform_ = w; phase_ = 0.0; sweepPhase_ = 0.0; }
    void setFrequency(float hz) { frequency_ = hz; }
    void setSweepRange(float startHz, float endHz, float seconds) {
        sweepStartHz_ = startHz; sweepEndHz_ = endHz; sweepSeconds_ = seconds;
    }
    void setLevelDb(float db) { amplitude_ = std::pow(10.0f, db / 20.0f); }
    Waveform waveform() const { return waveform_; }

    // Rellena un canal mono con la señal
    void fill(float* out, int count, float sampleRate);

    // Genera un barrido logarítmico completo (20 Hz–20 kHz por defecto)
    static std::vector<float> makeLogSweep(float startHz, float endHz, float seconds, float sampleRate);

private:
    Waveform waveform_ = Waveform::Silence;
    float frequency_ = 1000.0f;
    float amplitude_ = 0.5f;
    float sweepStartHz_ = 20.0f, sweepEndHz_ = 20000.0f, sweepSeconds_ = 5.0f;
    double phase_ = 0.0;
    double sweepPhase_ = 0.0;
    // Estado del filtro de ruido rosa (aproximación -3 dB/octava)
    float pinkB0_ = 0, pinkB1_ = 0, pinkB2_ = 0, pinkB3_ = 0, pinkB4_ = 0, pinkB5_ = 0, pinkB6_ = 0;
};

} // namespace acoustical
