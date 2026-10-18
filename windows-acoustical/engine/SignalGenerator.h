// SignalGenerator.h — generador de señales: seno por banda, barrido
// logarítmico, ruido rosa y blanco, con nivel y fundido de entrada/salida.
//
// UNIFICACIÓN (v1.8): la generación en sí (fases del seno/barrido, filtro de
// Paul Kellet del rosa, PRNG del blanco) vive en el núcleo C compartido
// (dsp/acoustical_dsp.c, acoustical_siggen_*): el mismo código corre aquí y
// en Android (vía JNI), y los tests del gate lo validan. Esta clase es el
// wrapper C++: solo publica los parámetros y delega el rellado del bloque.
//
// CONTRATO DE THREADS: la app Windows comparte UNA instancia entre la UI
// (setWaveform/setFrequency/setLevelDb/setSweepRange desde el SettingsPanel)
// y el callback de audio (fill() desde ConsoleComponent/plugin). Los
// parámetros son atómicos y fill() toma un SNAPSHOT de bloque (una lectura de
// cada atómico al inicio): dentro de un bloque el audio siempre ve un
// conjunto coherente de parámetros. El handle del core (fases, filtro rosa,
// PRNG) lo crea el constructor y solo lo toca el hilo de audio; al cambiar de
// forma de onda el core resetea su propio estado en ese hilo — nunca la UI
// escribe en el estado de generación (antes eso era un data race con
// transientes de nivel al volver a rosa).
#pragma once

#include <cmath>
#include "AcousticalParameters.h"
#include "acoustical_dsp.h"
#include <atomic>
#include <vector>

namespace acoustical {

class SignalGenerator {
public:
    // Valores del enum SIN cambiar (los menús y el mapeo del plugin los
    // usan); la conversión a la forma canónica del core es canonicalWaveform().
    enum class Waveform { Sine, BandSine, LogSweep, PinkNoise, WhiteNoise, Silence };

    SignalGenerator() : handle_(acoustical_siggen_create()) {}
    ~SignalGenerator() { acoustical_siggen_free(handle_); }
    SignalGenerator(const SignalGenerator&) = delete;
    SignalGenerator& operator=(const SignalGenerator&) = delete;

    // === Writer (cualquier hilo, normalmente la UI) ===

    // Publica la nueva forma de onda. El reset de fases y del filtro rosa NO
    // se hace aquí (sería una escritura compartida en pleno audio): lo hace
    // el core en fill() (hilo de audio) cuando detecta el cambio de bloque a
    // bloque.
    void setWaveform(Waveform w) {
        waveform_.store(static_cast<int>(w), std::memory_order_release);
    }
    void setFrequency(float hz) {
        frequency_.store(hz, std::memory_order_release);
    }
    void setSweepRange(float startHz, float endHz, float seconds) {
        // El rango se publica; el tiempo/fase del barrido vive en el core
        // (estado exclusivo del hilo de audio; un cambio de rango no salta la
        // fase dentro de un barrido).
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
    float frequency() const {
        return frequency_.load(std::memory_order_acquire);
    }
    float levelDb() const {
        const float a = amplitude_.load(std::memory_order_acquire);
        return a > 0.0f ? static_cast<float>(20.0 * std::log10(a)) : -120.0f;
    }

    // Mapea el enum local al valor canónico del core C (compartido con
    // Android; 0 = Silencio es la primera entrada de todos los menús).
    static int canonicalWaveform(Waveform w) {
        switch (w) {
            case Waveform::Silence:  return ACOUSTICAL_WAVE_SILENCE;
            case Waveform::Sine:     return ACOUSTICAL_WAVE_SINE;
            case Waveform::BandSine: return ACOUSTICAL_WAVE_BAND_SINE;
            case Waveform::LogSweep: return ACOUSTICAL_WAVE_LOG_SWEEP;
            case Waveform::PinkNoise:return ACOUSTICAL_WAVE_PINK;
            case Waveform::WhiteNoise:return ACOUSTICAL_WAVE_WHITE;
        }
        return ACOUSTICAL_WAVE_SILENCE;
    }

    // Inverso de canonicalWaveform(): de un valor canónico del core
    // (ACOUSTICAL_WAVE_*; el mapeo del menú del plugin, dsp/plugin_genmap.h,
    // devuelve estos) al enum local. Valores no canónicos → Silence: la rama
    // más segura nunca emite una señal equivocada.
    static Waveform fromCanonical(int canonical) {
        switch (canonical) {
            case ACOUSTICAL_WAVE_SILENCE:   return Waveform::Silence;
            case ACOUSTICAL_WAVE_SINE:      return Waveform::Sine;
            case ACOUSTICAL_WAVE_BAND_SINE: return Waveform::BandSine;
            case ACOUSTICAL_WAVE_LOG_SWEEP: return Waveform::LogSweep;
            case ACOUSTICAL_WAVE_PINK:      return Waveform::PinkNoise;
            case ACOUSTICAL_WAVE_WHITE:     return Waveform::WhiteNoise;
        }
        return Waveform::Silence;
    }

    // === Audio (hilo de audio) ===

    // Rellena un canal mono con la señal. Toma un snapshot de los
    // parámetros atómicos al inicio del bloque (ver el header) y lo pasa al
    // core, que rellena el bloque con ese conjunto coherente.
    void fill(float* out, int count, float sampleRate);

    // Genera un barrido logarítmico completo (20 Hz–20 kHz por defecto)
    // como vector estático (lo usa la captura de referencia).
    static std::vector<float> makeLogSweep(float startHz, float endHz, float seconds, float sampleRate);

private:
    // === Parámetros: atómicos (la UI escribe, el audio lee) ===
    std::atomic<int> waveform_{static_cast<int>(Waveform::Silence)};
    std::atomic<float> frequency_{1000.0f};
    std::atomic<float> amplitude_{0.5f};
    std::atomic<float> sweepStartHz_{20.0f}, sweepEndHz_{20000.0f}, sweepSeconds_{5.0f};

    // === Estado de generación: en el core C, exclusivo del hilo de audio ===
    acoustical_siggen* handle_ = nullptr;
};

} // namespace acoustical
