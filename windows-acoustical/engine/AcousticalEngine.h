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
#include "Rt60Estimator.h"
#include "SignalGenerator.h"

#include <atomic>
#include <array>
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
    std::vector<float> noiseProfile;              // copia (segura de mantener)
    float rt60Ms = 0.0f;                          // RT60 estimado (reflexiones)

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
    // Red de seguridad: los callers ya llaman stop() antes de destruir el
    // motor (destructor del plugin, apagado de la app), pero si un host
    // (Cubase) decide el orden del teardown y el engine muere sin stop(), el
    // thread joinable sin joinear hace std::terminate() en ~thread(): abort
    // sin stack (se pierde el crash). stop() es idempotente, así que esto no
    // cuesta nada cuando no hace falta.
    ~AcousticalEngine();

    std::function<void(const AnalysisResult&)> onAnalysis;            // hilo del motor
    std::function<void(SweepProcess, const SweepStep&)> onSweep;      // decisión de un ecu
    std::function<void(float)> onNoiseCaptureProgress;
    std::function<void()> onNoiseCaptureComplete;
    std::function<void(const std::string&)> onStartFailed;

    // === Configuración (port de configure()) ===
    // Thread-safe: puede llamarse desde el hilo de mensajes (UI) con el
    // motor en marcha. NO desde el hilo de audio: toma stateMutex_, que el
    // hilo del motor sostiene durante la FFT. Para el cambio de sample rate
    // (prepareToPlay del plugin, hilo de audio) existe requestConfigure().
    void configure(const AudioConfig& config);
    // (Hilo de audio) Encola una config para que el motor la APLIQUE en su
    // siguiente tick, bajo el lock que el motor ya sostiene: el caller nunca
    // se bloquea en stateMutex_ (el motor lo sostiene durante la FFT, y un
    // configure() directo desde el callback de audio se colgaría hasta que
    // el motor la terminase). El motor decide en el tick si el cambio es
    // estructural (recrea) o ligero (actualiza en sitio), igual que con
    // configure().
    void requestConfigure(const AudioConfig& config);
    // Devuelven COPIAS bajo lock: config_/bandFrequencies_ los reescribe
    // configure() desde otro hilo (una referencia se quedaría colgada).
    AudioConfig config() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return config_;
    }
    std::vector<float> bandFrequencies() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return bandFrequencies_;
    }
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
    // Pide parada y JOINEA el hilo del motor. Idempotente. NO llamar desde
    // el hilo de audio (el join bloquearía el callback hasta que el motor
    // termine su tick → xrun): desde el audio se usa requestStop() y el join
    // lo hace el destructor (que corre en el hilo de mensajes al borrar el
    // processor).
    void stop();
    // Pide parada SIN joinear: el motor termina el tick en curso y sale del
    // bucle. El join lo hace stop() o el destructor (hilo de mensajes).
    void requestStop();
    bool isRunning() const { return running_.load(); }

    // === Referencia y ruido ===
    void captureReference();   // reinicia correcciones y barridos automáticamente
    void clearReference();
    bool isReferenceCaptured() const { return referenceCaptured_.load(); }
    void startNoiseCapture();
    void cancelNoiseCapture();
    void clearNoiseProfile();
    bool hasNoiseProfile() const;
    bool isNoiseCapturing() const;
    float noiseCaptureProgress() const;

    // Referencia desde el espectro ANTES de salir (pre-EQ): la ventana del
    // plugin pasa el último bloque de la fuente y aquí se convierte a bandas.
    // Igual que captureReference(), reinicia correcciones y barridos.
    void captureReferenceFrom(const std::vector<float>& preSamples, int sampleRate);
    float currentRt60Ms() const { return rt60_.currentRt60Ms(); }

    // Calibración del medidor SPL: ajuste (dB) sobre la referencia de +120 dB.
    void setSplCalibrationOffset(float adjustDb);
    float splCalibrationOffset() const;

    // === Los tres ecuas dinámicos ===
    SweeperProcessor& sweeperFor(int index);   // acceso a parámetros en vivo
    void setDynamicEqConfig(int index, const DynamicEqConfig& cfg);
    void setDynamicEqEnabled(int index, bool enabled);
    // Estado actual (copias bajo lock; para presets y UI)
    DynamicEqConfig dynamicEqConfig(int index) const;
    bool dynamicEqEnabled(int index) const;
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
    // Eventos de un tick que se recogen bajo stateMutex_ y se despachan fuera.
    struct TickEvents {
        std::vector<std::pair<SweepProcess, SweepStep>> sweeps;
        AnalysisResult analysis;
        bool hasAnalysis = false;
        float noiseProgress = -1.0f;  // -1 = sin progreso este tick
        bool noiseComplete = false;
    };

    void engineLoop();
    void analyzeFrameLocked(const std::vector<float>& samples, int sampleRate, TickEvents& ev);
    // Cuerpo de configure() sin lock: lo llaman configure() (que toma
    // stateMutex_ por el caller) y el tick del motor (que ya lo sostiene al
    // aplicar un requestConfigure encolado).
    void configureLocked(const AudioConfig& config);
    void setDynamicEqConfigLocked(int index, const DynamicEqConfig& cfg);
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
    std::array<std::unique_ptr<SweeperProcessor>, kDynamicEqCount> sweepers_{};
    int sweepersBandCount_ = -1;
    std::vector<DynamicEqConfig> dynamicCfgs_;
    std::vector<bool> sweeperEnabled_;
    std::vector<std::vector<SupportBand>> supportBandsByEq_;
    int eqSampleRate_ = 0;  // tasa real con la que se preparó el EqDsp (ver A3)

    std::vector<float> referenceLevels_;
    std::atomic<bool> referenceCaptured_{false};
    float splCalibrationDb_ = 0.0f;  // ajuste del usuario sobre la referencia +120

    // Sincronización:
    //  - stateMutex_ protege todo el estado estructural compartido entre la UI
    //    y el hilo del motor (config, bandas, fft, corrector, sweepers, …).
    //    El hilo de audio NO lo toca (por eso existen requestConfigure() y
    //    requestStop(): la cola de config y la parada se gestionan con
    //    requestMutex_/threadMutex_, y el motor los consume en su tick).
    //  - threadMutex_ protege start()/stop()/requestStop().
    //  - ringMutex_ protege el anillo de entrada (hilo de audio <-> motor).
    //  - requestMutex_ protege la config encolada (requestConfigure -> tick).
    mutable std::mutex stateMutex_;
    std::mutex threadMutex_;
    mutable std::mutex ringMutex_;
    std::mutex requestMutex_;
    AudioConfig pendingConfig_{};
    bool hasPendingConfig_ = false;
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
    Rt60Estimator rt60_;
    SignalGenerator generator_;
};

} // namespace acoustical
