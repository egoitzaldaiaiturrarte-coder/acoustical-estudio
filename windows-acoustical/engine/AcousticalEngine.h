// AcousticalEngine.h — port de AudioEngine.kt: análisis FFT, corrector de sala,
// los tres ecuas dinámicos (mismo corrector, distintos ajustes), perfil de
// ruido, SPL y bandas de apoyo por ecu. Acepta audio de cualquier fuente
// (captura del PC, bridge ASIO o plugin) vía pushSamples.
#pragma once

#include "AcousticalParameters.h"
#include "StandardFrequencies.h"
#include "FftProcessor.h"
#include "RoomCorrector.h"
#include "SplMeter.h"
#include "NoiseProfiler.h"
#include "SweeperProcessor.h"
#include "EqDsp.h"
#include "SignalGenerator.h"

#include <atomic>
#include <memory>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace acoustical {

struct AnalysisResult {
    SpectrumFrame spectrum;                       // espectro FFT (bins)
    std::vector<EqBand> bands;                    // EQ principal corregido
    std::vector<float> measuredBandLevels;        // niveles por banda
    float spl = 0.0f, peakSpl = 0.0f, averageSpl = 0.0f;
    float correctionIntensity = 0.0f;
    long long framesAnalyzed = 0;
    const std::vector<float>* noiseProfile = nullptr;  // propiedad del profiler

    // Curvas combinadas por banda (EQ manual + ecuas dinámicos + bandas de apoyo)
    std::vector<float> combinedGainsL;
    std::vector<float> combinedGainsR;
    // Curva por ecu dinámico (para los tres mini-EQs)
    std::vector<std::vector<float>> dynamicEqGainsL;
    std::vector<std::vector<float>> dynamicEqGainsR;

    // Bloque temporal reciente (osciloscopio)
    std::vector<float> timeSamples;
};

class AcousticalEngine {
public:
    AcousticalEngine();

    std::function<void(const AnalysisResult&)> onAnalysis;            // hilo del motor
    std::function<void(SweepProcess, const SweepStep&)> onSweep;      // decisión de un ecu
    std::function<void(float)> onNoiseCaptureProgress;
    std::function<void()> onNoiseCaptureComplete;
    std::function<void(const std::string&)> onStartFailed;

    // === Configuración (port de configure()) ===
    void configure(const AudioConfig& config);
    const AudioConfig& config() const { return config_; }
    const std::vector<float>& bandFrequencies() const { return bandFrequencies_; }
    std::vector<EqBand> bands() const;
    void setBandGain(int index, float gainDb);
    void resetBands();

    // === Audio ===
    // Alimenta el motor con audio mono (o el canal que se quiera analizar).
    void pushSamples(const float* samples, int count, int sampleRate);
    // Mezcla con una segunda entrada activa (puente ASIO, cable virtual…):
    // cada banda usa el nivel más fuerte de ambas, como en el móvil.
    void setSecondaryCaptureLevels(const std::vector<float>& levels);

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // === Referencia y ruido ===
    void captureReference();   // reinicia correcciones y barridos automáticamente
    void clearReference();
    bool isReferenceCaptured() const { return referenceCaptured_.load(); }
    void startNoiseCapture();
    void cancelNoiseCapture();
    void clearNoiseProfile();
    bool hasNoiseProfile() const;

    // === Los tres ecuas dinámicos ===
    SweeperProcessor& sweeperFor(int index);   // acceso a parámetros en vivo
    void setDynamicEqConfig(int index, const DynamicEqConfig& cfg);
    void setDynamicEqEnabled(int index, bool enabled);
    void setDynamicEqInterval(int index, int ms);
    void setDynamicEqMaxGain(int index, float gainDb);
    void setDynamicEqSpeed(int index, float speed);
    void setDynamicEqExtras(int index, int extras);
    void setDynamicEqMixerLevel(int index, float level);
    void setEqChannelLinked(bool linked);   // enlazado: cada ecu corrige L+R juntos
    void setSupportBands(int index, const std::vector<SupportBand>& bands);

    // === DSP en tiempo real ===
    EqDsp& eqDspL() { return dspL_; }
    EqDsp& eqDspR() { return dspR_; }

    SignalGenerator& signalGenerator() { return generator_; }

private:
    void engineLoop();
    void analyzeFrame(const std::vector<float>& samples, int sampleRate);
    float supportGainAt(float freqHz) const;
    float supportTaper(float freqHz, const SupportBand& band) const;
    std::vector<float> addSupportGains(const std::vector<float>& gains, int eqIndex) const;
    SpectrumFrame applySupportBands(const SpectrumFrame& spectrum) const;

    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    AudioConfig config_;
    std::vector<float> bandFrequencies_;
    std::vector<EqBand> bands_;
    std::unique_ptr<FftProcessor> fft_;
    std::unique_ptr<RoomCorrector> corrector_;
    std::unique_ptr<SplMeter> splMeter_;
    std::unique_ptr<NoiseProfiler> noiseProfiler_;
    std::vector<std::unique_ptr<SweeperProcessor>> sweepers_;
    std::vector<DynamicEqConfig> dynamicCfgs_;
    std::vector<bool> sweeperEnabled_;
    std::vector<std::vector<SupportBand>> supportBandsByEq_;

    std::vector<float> referenceLevels_;
    std::atomic<bool> referenceCaptured_{false};

    // Anillo de audio de entrada (pushSamples desde el hilo de audio)
    mutable std::mutex ringMutex_;
    std::deque<float> ring_;
    size_t ringCapacity_ = 8192 * 4;
    std::atomic<int> inputSampleRate_{48000};

    std::vector<float> lastMeasuredLevels_;
    std::vector<float> secondaryLevels_;
    std::mutex secondaryMutex_;

    std::thread engineThread_;
    std::atomic<bool> running_{false};
    std::atomic<long long> framesAnalyzed_{0};
    std::atomic<long long> lastAnalysisMs_{0};

    EqDsp dspL_, dspR_;
    SignalGenerator generator_;
};

} // namespace acoustical
