#include "PluginProcessor.h"
#include "PluginEditor.h"

// === Parámetros: inventario completo por ecu dinámico + master ===
juce::AudioProcessorValueTreeState::ParameterLayout
AcousticalAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"correction", 1}, juce::String::fromUTF8("Corrección"), true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"maxGain", 1}, juce::String::fromUTF8("Ganancia máxima"), 1.0f, 50.0f, 12.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"smoothing", 1}, "Suavizado", 0.05f, 0.8f, 0.3f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"noiseSub", 1}, juce::String::fromUTF8("Sustracción de ruido"), true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"link", 1}, "Canales L/R enlazados", true));

    for (int i = 1; i <= 3; ++i) {
        const juce::String n = juce::String(i);
        const juce::String prefix = "eq" + n + ".";
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"eq" + n + "Enabled", 1}, "Ecu " + n + " activo", true));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"eq" + n + "Interval", 1}, "Ecu " + n + " intervalo (ms)",
            100, 2000, 800));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Gain", 1}, "Ecu " + n + " ganancia",
            1.0f, 50.0f, 12.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Mixer", 1}, "Ecu " + n + " mezclador",
            0.0f, 1.0f, 0.8f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Speed", 1}, "Ecu " + n + " velocidad",
            0.5f, 4.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"eq" + n + "Extras", 1}, "Ecu " + n + " barridos extra",
            0, 6, 1));
    }
    return layout;
}

AcousticalAudioProcessor::AcousticalAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()) {
    // AudioProcessorParameter::Listener (APVTS no tiene addListener en la JUCE
    // del proyecto) — cubre tanto los cambios de la ventana como la
    // automatización del host.
    for (auto* param : getParameters()) param->addListener(this);
    engine_.onSweep = [this](acoustical::SweepProcess p, const acoustical::SweepStep& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        sweepSteps_[static_cast<int>(p)] = s;
    };
    // El análisis lo publica el hilo del motor: se registra UNA vez aquí
    // (antes se reasignaba dentro de processBlock, con carrera de datos).
    engine_.onAnalysis = [this](const acoustical::AnalysisResult& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::make_unique<acoustical::AnalysisResult>(r);
    };
    applyParameters();
}

AcousticalAudioProcessor::~AcousticalAudioProcessor() {
    cancelPendingUpdate();
    for (auto* param : getParameters()) param->removeListener(this);
    engine_.stop();
}

void AcousticalAudioProcessor::handleAsyncUpdate() { applyParameters(); }

// Llamado desde cualquier hilo (host o UI): coalescemos con AsyncUpdater y
// aplicamos en el hilo de mensajes.
void AcousticalAudioProcessor::parameterValueChanged(int, float) { triggerAsyncUpdate(); }
void AcousticalAudioProcessor::parameterGestureChanged(int, bool) {}

void AcousticalAudioProcessor::applyParameters() {
    // configure() es thread-safe (stateMutex_ en el motor): se puede llamar
    // con el motor en marcha, sin detenerlo. Antes se hacía stop()/start()
    // en cada slider, y en el caso de prepareToPlay ese stop() era un join
    // DE Hilo de audio (C3/M10).
    auto c = engine_.config();
    c.correctionEnabled = apvts.getRawParameterValue("correction")->load() > 0.5f;
    c.maxGainDb = apvts.getRawParameterValue("maxGain")->load();
    c.smoothingFactor = apvts.getRawParameterValue("smoothing")->load();
    c.noiseSubtractionEnabled = apvts.getRawParameterValue("noiseSub")->load() > 0.5f;
    engine_.configure(c);

    engine_.setEqChannelLinked(apvts.getRawParameterValue("link")->load() > 0.5f);
    for (int i = 0; i < 3; ++i) {
        const juce::String n = juce::String(i + 1);
        engine_.setDynamicEqEnabled(i, apvts.getRawParameterValue("eq" + n + "Enabled")->load() > 0.5f);
        acoustical::DynamicEqConfig cfg;
        cfg.decisionIntervalMs = static_cast<int>(apvts.getRawParameterValue("eq" + n + "Interval")->load());
        cfg.maxGainDb = apvts.getRawParameterValue("eq" + n + "Gain")->load();
        cfg.mixerLevel = apvts.getRawParameterValue("eq" + n + "Mixer")->load();
        cfg.speedMultiplier = apvts.getRawParameterValue("eq" + n + "Speed")->load();
        cfg.extraSweeps = static_cast<int>(apvts.getRawParameterValue("eq" + n + "Extras")->load());
        cfg.startFrom = static_cast<acoustical::SweepDirection>(i);  // NEED_BASED/BOTTOM_UP/TOP_DOWN
        engine_.setDynamicEqConfig(i, cfg);
    }
}

void AcousticalAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    // prepareToPlay corre en el HILO de audio: está prohibido hacer stop()
    // (join de un hilo) ni trabajo pesado aquí. configure() es thread-safe y
    // start() es idempotente, así que basta con reconfigurar y arrancar.
    auto c = engine_.config();
    const double sr = sampleRate;
    c.sampleRate = sr > 87000.0 ? acoustical::SampleRate::Hz96000
                 : sr > 66000.0 ? acoustical::SampleRate::Hz88200
                 : sr > 46000.0 ? acoustical::SampleRate::Hz48000
                                : acoustical::SampleRate::Hz44100;
    engine_.configure(c);
    applyParameters();  // reaplica los parámetros tras reconfigurar el motor
    engine_.start();    // sin-op si ya estaba en marcha
}

void AcousticalAudioProcessor::releaseResources() { engine_.stop(); }

void AcousticalAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // 1. Análisis: la señal que pasa por el canal alimenta el motor
    const int numInputs = getTotalNumInputChannels();
    if (monoBuf_.size() < static_cast<size_t>(numSamples)) monoBuf_.resize(numSamples);
    float* mono = monoBuf_.data();
    std::fill_n(mono, numSamples, 0.0f);
    for (int ch = 0; ch < numInputs; ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s) mono[s] += src[s];
    }
    if (numInputs == 0) {
        for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch) {
            const float* src = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s) mono[s] += src[s];
        }
    }
    engine_.pushSamples(mono, numSamples, static_cast<int>(getSampleRate()));

    // 2. EQ en tiempo real por canal (correcciones de los tres ecuas dinámicos)
    if (leftBuf_.size() < static_cast<size_t>(numSamples)) leftBuf_.resize(numSamples);
    if (rightBuf_.size() < static_cast<size_t>(numSamples)) rightBuf_.resize(numSamples);
    float* left = leftBuf_.data();
    float* right = rightBuf_.data();
    std::copy_n(buffer.getReadPointer(0), numSamples, left);
    std::copy_n(buffer.getReadPointer(getTotalNumOutputChannels() > 1 ? 1 : 0), numSamples,
                right);
    engine_.eqDspL().process(left, numSamples);
    engine_.eqDspR().process(right, numSamples);
    buffer.copyFrom(0, 0, left, numSamples);
    if (getTotalNumOutputChannels() > 1) buffer.copyFrom(1, 0, right, numSamples);
}

juce::AudioProcessorEditor* AcousticalAudioProcessor::createEditor() {
    return new AcousticalEditor(*this);
}

void AcousticalAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void AcousticalAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            applyParameters();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AcousticalAudioProcessor();
}
