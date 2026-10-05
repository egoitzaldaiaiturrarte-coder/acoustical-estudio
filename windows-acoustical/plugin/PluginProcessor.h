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

#include <atomic>
#include <deque>
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
    // referencia al pulsar "Capturar referencia". Mismo patrón de mutex que el
    // anillo del motor (hilo de audio <-> hilo de mensajes).
    mutable std::mutex preMutex_;
    std::deque<float> preRing_;

    // Línea de retardo global (0–100 ms, pasos de 0,01 ms), solo hilo de
    // audio; la latencia se reporta al host para que Cubase compense (PDC).
    std::vector<float> delayRing_[2];
    int delayWritePos_ = 0;
    int delaySamples_ = 0;
    int reportedLatency_ = 0;

    // Generador de referencia (estado solo-hilo-de-audio; los parámetros se
    // leen de los atómicos del APVTS en cada bloque).
    acoustical::SignalGenerator generator_;
    acoustical::SignalGenerator::Waveform genWaveform_ =
        acoustical::SignalGenerator::Waveform::Silence;
    float genFrequency_ = 1000.0f;
    float genLevelDb_ = -12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AcousticalAudioProcessor)
};
