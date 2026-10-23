// SpatialProcessor.h — "Acoustical Spatial" (v1.11.0): el VST COMPLEMENTARIO
// del Acoustical Dynamic EQ. Su única función es la POSICIÓN ESPACIAL del
// elemento, con el panel grande y cómodo (el "pantallazo" del móvil):
// joystick x/y + faders de distancia/tamaño + entorno, en vez de los cuatro
// sliders en fila del editor del VST principal.
//
// El audio que pasa por el plugin recibe la MISMA compensación espacial que
// aplica el VST principal, la app Windows y el móvil (la ley del core C,
// acoustical_spatial_compensate): ganancia por distancia/tamaño/offset y
// retardo de propagación según z. En la posición neutra (x=0, y=0, z=0.5,
// tamaño 0.5) la ganancia es exactamente unidad y el retardo 0 ms: insertar
// el plugin en una pista no la cambia.
//
// Cada instancia tiene su propia posición (igual que el VST principal): los
// parámetros spatialX/spatialY/spatialZ/spatialSize se guardan en el estado
// del proyecto y se automatizan como en el resto del plugin.
//
// El parámetro "environment" (Sala/Estudio/Escenario/Exterior) es solo visual
// (fondo del joystick y metros de la lectura; las mismas 4 dimensiones de
// WorkEnvironmentType del móvil): la ley de compensación no depende de él.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <acoustical_dsp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

class AcousticalSpatialProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorParameter::Listener {
public:
    AcousticalSpatialProcessor();
    ~AcousticalSpatialProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Acoustical Spatial"; }
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

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool) override {}

    juce::AudioProcessorValueTreeState apvts;

    // Punteros a los atómicos que el APVTS mantiene (los resuelvo UNA vez en
    // el constructor: buscar por id en cada bloque haría malloc en el audio).
    std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY = nullptr;
    std::atomic<float>* pZ = nullptr;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pEnv = nullptr;

    double sampleRate_ = 44100.0;

    // Anillos de retardo por canal (solo los toca el hilo de audio). El
    // retardo máximo de la ley es (15 − 7,5) m / 343 m/s ≈ 21,9 ms; la
    // capacidad reserva margen amplio (50 ms a cualquier tasa de muestreo).
    static constexpr int NUM_RINGS = 2;
    std::array<std::vector<float>, NUM_RINGS> rings_{};
    std::array<int, NUM_RINGS> ringCap_ = {0, 0};
    std::array<int, NUM_RINGS> writePos_ = {0, 0};
};
