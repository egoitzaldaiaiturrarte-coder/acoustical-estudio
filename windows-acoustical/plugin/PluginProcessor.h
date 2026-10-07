// PluginProcessor.h — "Acoustical Dynamic EQ": el mismo motor de la app móvil
// (corrector principal + los tres ecuas dinámicos idénticos con distintos
// ajustes) dentro de Cubase como VST3 / VST2.4, autosuficiente.
//
// Bucle cerrado: captura la REFERENCIA del espectro ANTES de salir (pre-EQ,
// lo que la fuente envía — con ayuda del generador integrado), mide el
// espectro DESPUÉS (post-EQ, lo que sale hacia los altavoces) y lo corrige
// hacia la referencia en tiempo real con cuatro procesos: el motor más los
// tres ecuas dinámicos, cada uno con su arranque, intervalo, ganancia,
// mezclador, velocidad y barridos extra.
//
// El ruido se trabaja de dos maneras, igual que en el móvil:
//  - En los silencios: perfil de ruido capturado y restado por compuerta
//    espectral para que no contamine la corrección.
//  - En reproducción: los ecuas dinámicos cortan los picos y reflexiones que
//    sobresalen de la media (RT60 visible como indicador de la sala).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <AcousticalEngine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class AcousticalAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorParameter::Listener,
                                 private juce::AsyncUpdater {
public:
    AcousticalAudioProcessor();
    ~AcousticalAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Acoustical Dynamic EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    acoustical::AcousticalEngine& engine() { return engine_; }
    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // === Acciones desde la ventana (hilo de mensajes) ===
    // Referencia = espectro ANTES de salir: FFT del anillo pre-EQ.
    void captureReferenceFromInput();
    void clearReference() { engine_.clearReference(); }
    void startNoiseCapture() { engine_.startNoiseCapture(); }
    void cancelNoiseCapture() { engine_.cancelNoiseCapture(); }
    void clearNoiseProfile() { engine_.clearNoiseProfile(); }

    // Copia thread-safe del último análisis para el editor
    void getLatestAnalysis(acoustical::AnalysisResult& out,
                           acoustical::SweepStep sweepsOut[3]) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latest_) out = *latest_;
        for (int i = 0; i < 3; ++i) sweepsOut[i] = sweepSteps_[i];
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyParameters();

private:
    // Listener de parámetros (AudioProcessorParameter::Listener, compatible
    // con la JUCE del proyecto y con la automatización del host): coalescemos
    // los cambios (un arrastre de slider genera decenas) con AsyncUpdater y
    // aplicamos en el hilo de mensajes.
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    acoustical::AcousticalEngine engine_;
    juce::AudioProcessorValueTreeState apvts;

    mutable std::mutex mutex_;
    std::unique_ptr<acoustical::AnalysisResult> latest_;
    acoustical::SweepStep sweepSteps_[3]{};

    // Buffers preasignados de processBlock: solo los toca el hilo de audio;
    // se redimensionan solo si crecen (sin malloc en estado estable).
    mutable std::vector<float> monoBuf_, leftBuf_, rightBuf_, genBuf_;

    // Anillo pre-EQ (lo que la fuente envía, antes de salir): de aquí sale la
    // referencia al pulsar "Capturar referencia".
    //
    // Anillo FIJO preasignado (array de capacidad máxima + head/tail
    // atómicos), mismo patrón que RouteHub::Ring / RemoteAudioLink::Ring en
    // app/: el std::deque + std::mutex anterior hacía reallocs en cada push
    // (malloc en el path RT) y un lock por bloque.
    //
    // Propiedad del anillo (SPSC, sin lock en el path de audio):
    //  - el thread de audio es el ÚNICO escritor: avanza la cabeza de
    //    escritura w y, si el anillo se llena, descarta lo más antiguo
    //    (avanzando r) — la misma "conservar las CAP últimas" que el deque
    //    viejo. El reader nunca escribe r, así que no puede chocar con ese
    //    descarte.
    //  - el reader es la captura del engine (captureReferenceFromInput, hilo
    //    de mensajes): solo LEe los punteros (w, r) y copia las últimas
    //    muestras. El "pop" de datos lo hace quien consume (el engine),
    //    manteniendo la semántica anterior: la captura recibe toda la
    //    ventana reciente.
    struct PreRing {
        static constexpr int CAP = 32768;  // potencia de 2, como el deque viejo
        std::array<float, CAP> data{};
        std::atomic<std::int64_t> w{0}, r{0};

        // (Hilo de audio). El descarte usa max(rv, wv + n - CAP) en vez de
        // un if de desbordamiento: con n <= CAP es idéntico (solo avanza si el
        // anillo se llena), y si un bloque llegara a superar CAP (n > CAP,
        // imposible en la práctica pero JUCE no garantiza un máximo) r se
        // queda a CAP de la nueva cabeza y no la sobrepasa: la ventana de
        // lectura [r, w) nunca queda vacía/negativa para el reader.
        void write(const float* s, int n) {
            const std::int64_t wv = w.load(std::memory_order_relaxed);
            const std::int64_t rv = r.load(std::memory_order_relaxed);
            r.store(std::max(rv, wv + n - CAP), std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
                data[static_cast<int>((wv + i) & (CAP - 1))] = s[i];
            w.store(wv + n, std::memory_order_release);
        }
        // (Hilo de mensajes: la captura del engine) — solo lectura de
        // punteros: copia las `take` últimas muestras en `out`.
        // (No const: en C++17 std::atomic::load() no es miembro const.)
        void readLatest(std::vector<float>& out) {
            const std::int64_t wv = w.load(std::memory_order_acquire);
            const std::int64_t rv = r.load(std::memory_order_acquire);
            const int take = static_cast<int>(std::min<std::int64_t>(CAP, wv - rv));
            out.resize(take);
            for (int i = 0; i < take; ++i) {
                const std::int64_t src = wv - take + i;
                out[i] = data[static_cast<int>(src & (CAP - 1))];
            }
        }
    };
    PreRing preRing_;

    // Línea de retardo global (0–100 ms, pasos de 0,01 ms), solo hilo de
    // audio; la latencia se reporta al host para que Cubase compense (PDC).
    std::vector<float> delayRing_[2];
    int delayWritePos_ = 0;
    int delaySamples_ = 0;
    int reportedLatency_ = 0;

    // Cota conservadora del retardo de grupo del banco de biquads del EQ
    // (peaking RBJ, uno por banda), que se suma a la línea de retardo en el
    // PDC (setLatencySamples).
    //
    // Razonamiento: el PDC solo modela un retardo CONSTANTE, pero el banco
    // tiene un retardo de grupo (dependiente de la frecuencia): cada biquad
    // rota fase rápidamente cerca de su frecuencia central y el retardo de
    // grupo de un par de polos 2nd-order pegado a la unidad es inversamente
    // proporcional a la distancia al DC (≈ Q·fs/(2π·f0) cerca de la
    // resonancia). Se midió el retardo de grupo TOTAL del banco real del
    // plugin (barrido de 20 Hz–20 kHz por 1/6…1/12 de octava, Q=1.41,
    // ganancias peorcaso ±6 dB, las 5 tasas soportadas) buscando el máximo
    // sobre la frecuencia: el pico siempre cae en la banda más grave (20
    // Hz, su polo es el más cercano al DC) y valía ~47 muestras a 44.1/48
    // kHz, ~65 a 88.2 kHz y ~108 a 96 kHz (el caso 10 bandas: bandas más
    // anchas → resonancia más profunda → fase más lenta). 128 muestras cubre
    // todos esos máximos (y cualquier configuración de bandas del plugin)
    // con margen: son ~2.6 ms a 48 kHz y ~1.3 ms a 96 kHz, despreciables
    // frente a la línea de retardo de 0–100 ms del usuario. (Una cota "1-2
    // muestras por biquad" no cerraba el caso de 10 bandas, y "2 por banda
    // activa" sobreestimaría el de 124 bandas: 248 vs ~70 medidas.)
    static constexpr int kEqGroupDelaySamples = 128;

    // Rate del bloque con la que se preparó el EQ (solo hilo de audio):
    // processBlock re-prepara los biquads si el host cambió la sample rate
    // entre bloques (ver el comentario de processBlock).
    int eqPreparedRate_ = 0;

    // Generador de referencia (estado solo-hilo-de-audio; los parámetros se
    // leen de los atómicos del APVTS en cada bloque).
    acoustical::SignalGenerator generator_;
    acoustical::SignalGenerator::Waveform genWaveform_ =
        acoustical::SignalGenerator::Waveform::Silence;
    float genFrequency_ = 1000.0f;
    // Valor envenenado (fuera del rango del slider [-60, 0]): en el primer
    // bloque activo se aplica SIEMPRE el nivel del menú. Con el antiguo -12
    // (coincidencia con el default del parámetro) el generador arrancaba con
    // su default interno (amplitud 0.5 = -6 dB) en vez de los -12 pedidos.
    float genLevelDb_ = -999.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcousticalAudioProcessor)
};
