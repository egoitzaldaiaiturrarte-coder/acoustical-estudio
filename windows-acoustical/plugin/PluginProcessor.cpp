#include "PluginProcessor.h"
#include "PluginEditor.h"

// === Parámetros: inventario completo por ecu dinámico + master ===
juce::AudioProcessorValueTreeState::ParameterLayout
AcousticalAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"correction", 1}, "Corrección", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"maxGain", 1}, "Ganancia máxima", 1.0f, 50.0f, 12.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"smoothing", 1}, "Suavizado", 0.05f, 0.8f, 0.3f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"noiseSub", 1}, "Sustracción de ruido", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"link", 1}, "Canales L/R enlazados", true));

    for (int i = 1; i <= 3; ++i) {
        const juce::String n = juce::String(i);
        const juce::String prefix = "eq" + n + ".";
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"eq" + n + "Enabled", 1}, "Ecu " + n + " activo", i == 1));
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
    engine_.onSweep = [this](acoustical::SweepProcess p, const acoustical::SweepStep& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        sweepSteps_[static_cast<int>(p)] = s;
    };
    applyParameters();
}

void AcousticalAudioProcessor::applyParameters() {
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
    // Mapea al muestreo soportado más cercano
    auto c = engine_.config();
    const double sr = sampleRate;
    c.sampleRate = sr > 87000.0 ? acoustical::SampleRate::Hz96000
                 : sr > 66000.0 ? acoustical::SampleRate::Hz88200
                 : sr > 46000.0 ? acoustical::SampleRate::Hz48000
                                : acoustical::SampleRate::Hz44100;
    engine_.configure(c);
    engine_.start();
}

void AcousticalAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // 1. Análisis: la señal que pasa por el canal alimenta el motor
    const int numInputs = getTotalNumInputChannels();
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
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
    engine_.pushSamples(mono.data(), numSamples, static_cast<int>(getSampleRate()));

    // 2. EQ en tiempo real por canal (correcciones de los tres ecuas dinámicos)
    std::vector<float> left(static_cast<size_t>(numSamples));
    std::vector<float> right(static_cast<size_t>(numSamples));
    std::copy_n(buffer.getReadPointer(0), numSamples, left.begin());
    std::copy_n(buffer.getReadPointer(getTotalNumOutputChannels() > 1 ? 1 : 0), numSamples,
                right.begin());
    engine_.eqDspL().process(left.data(), numSamples);
    engine_.eqDspR().process(right.data(), numSamples);
    buffer.copyFrom(0, 0, left.data(), numSamples);
    if (getTotalNumOutputChannels() > 1) buffer.copyFrom(1, 0, right.data(), numSamples);

    // 3. Snapshot para el editor
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = nullptr;  // el análisis real lo publica el hilo del motor vía onAnalysis
    }
    // El hilo del motor publica onAnalysis → guardamos ahí
    engine_.onAnalysis = [this](const acoustical::AnalysisResult& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::make_unique<acoustical::AnalysisResult>(r);
    };
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
