// SignalGenerator.h — port ampliado del generador de señales: seno por banda,
// barrido logarítmico, ruido rosa y blanco, con nivel y fundido de entrada/salida.
//
// CONTRATO DE THREADS: la app Windows comparte UNA instancia entre la UI
// (setWaveform/setFrequency/setLevelDb/setSweepRange desde el SettingsPanel)
// y el callback de audio (fill() desde ConsoleComponent/plugin). Antes, los
// parámetros eran miembros planos: la UI escribía mientras el audio leía →
// data race (lecturas torpedeadas, pops; con el LogSweep, saltos de fase).
// Ahora los parámetros son atómicos y fill() toma un SNAPSHOT de bloque
// (una lectura de cada atómico al inicio): dentro de un bloque el audio
// siempre ve un conjunto coherente de parámetros. Las fases y el estado del
// filtro rosa son exclusivos del hilo de audio.
#pragma once

#include "AcousticalParameters.h"
#include <atomic>
#include <vector>

namespace acoustical {

class SignalGenerator {
public:
    enum class Waveform { Sine, BandSine, LogSweep, PinkNoise, WhiteNoise, Silence };

    // === Writer (cualquier hilo, normalmente la UI) ===

    // Publica la nueva forma de onda. El reset de fases y del filtro rosa NO
    // se hace aquí (sería una escritura compartida en pleno audio): lo hace
    // fill() en el hilo de audio cuando detecta el cambio de bloque a bloque.
    void setWaveform(Waveform w) {
        waveform_.store(static_cast<int>(w), std::memory_order_release);
    }
    void setFrequency(float hz) {
        frequency_.store(hz, std::memory_order_release);
    }
    void setSweepRange(float startHz, float endHz, float seconds) {
        // El rango se publica; el tiempo/fase del barrido vive en el hilo de
        // audio (un cambio de rango no salta la fase dentro de un barrido).
        sweepStartHz_.store(startHz, std::memory_order_release);
        sweepEndHz_.store(endHz, std::memory_order_release);
        sweepSeconds_.store(seconds, std::memory_order_release);
    }
    void setLevelDb(float db) {
        amplitude_.store(std::pow(10.0f, db / 20.0f), std::memory_order_release);
    }
    Waveform waveform() const {
        return static_cast<Waveform>(waveform_.load(std::memory_order_acquire));
    }

    // === Audio (hilo de audio) ===

    // Rellena un canal mono con la señal. Toma un snapshot de los
    // parámetros atómicos al inicio del bloque (ver el header).
    void fill(float* out, int count, float sampleRate);

    // Genera un barrido logarítmico completo (20 Hz–20 kHz por defecto)
    static std::vector<float> makeLogSweep(float startHz, float endHz, float seconds, float sampleRate);

private:
    // === Parámetros: atómicos (la UI escribe, el audio lee) ===
    std::atomic<int> waveform_{static_cast<int>(Waveform::Silence)};
    std::atomic<float> frequency_{1000.0f};
    std::atomic<float> amplitude_{0.5f};
    std::atomic<float> sweepStartHz_{20.0f}, sweepEndHz_{20000.0f}, sweepSeconds_{5.0f};

    // === Solo el hilo de audio (fill) ===
    int prevWaveform_ = static_cast<int>(Waveform::Silence);  // snapshot del bloque anterior
    double phase_ = 0.0;
    double sweepPhase_ = 0.0;   // tiempo t (s) dentro del barrido
    double sweepIntPhase_ = 0.0; // fase integrada del barrido (radianes)
    // Estado del filtro de ruido rosa (aproximación -3 dB/octava)
    float pinkB0_ = 0, pinkB1_ = 0, pinkB2_ = 0, pinkB3_ = 0, pinkB4_ = 0, pinkB5_ = 0, pinkB6_ = 0;
};

} // namespace acoustical
