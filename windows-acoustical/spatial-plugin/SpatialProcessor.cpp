// SpatialProcessor.cpp — implementación del "Acoustical Spatial" (v1.11.0).
// Ver SpatialProcessor.h: passthrough + compensación espacial (misma ley del
// core C que el VST principal, la app Windows y el móvil).
#include "SpatialProcessor.h"
#include "SpatialEditor.h"

#include <cmath>

using juce::String;
// utf8() viene del anonymous namespace de SpatialEditor.h (incluido arriba).

juce::AudioProcessorValueTreeState::ParameterLayout
AcousticalSpatialProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Posición espacial (mismos ids y rangos que el VST principal:
    // spatialX/Y −1..1, spatialZ/size 0..1).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"spatialX", 1}, utf8("Espacio X (izquierda↔derecha)"),
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.05f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"spatialY", 1}, utf8("Espacio Y (frente↔detrás)"),
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.05f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"spatialZ", 1}, utf8("Espacio Z (cerca↔lejos)"),
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.05f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"spatialSize", 1}, utf8("Tamaño del elemento"),
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.05f), 0.5f));

    // Entorno de trabajo (solo visual: fondo del joystick y metros de la
    // lectura; las 4 dimensiones de WorkEnvironmentType del móvil).
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"environment", 1}, utf8("Entorno de trabajo"),
        juce::StringArray{utf8("Sala (8×10 m)"), utf8("Estudio (6×7 m)"),
                           utf8("Escenario (12×8 m)"), utf8("Exterior (20×30 m)")},
        0));
    return layout;
}

AcousticalSpatialProcessor::AcousticalSpatialProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()) {
    for (auto* param : getParameters()) param->addListener(this);
    // Los atómicos que el audio thread lee: resueltos una vez (ver la nota
    // del VST principal: buscar por id por bloque hace malloc en el RT).
    pX = apvts.getRawParameterValue("spatialX");
    pY = apvts.getRawParameterValue("spatialY");
    pZ = apvts.getRawParameterValue("spatialZ");
    pSize = apvts.getRawParameterValue("spatialSize");
    pEnv = apvts.getRawParameterValue("environment");
}

AcousticalSpatialProcessor::~AcousticalSpatialProcessor() {
    for (auto* param : getParameters()) param->removeListener(this);
}

void AcousticalSpatialProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = sampleRate;
    // Capacidad de anillo: 50 ms a la tasa de muestreo (el retardo máximo de
    // la ley es ~22 ms; el margen cubre el redondeo y futuras ley).
    int cap = static_cast<int>(sampleRate * 0.05);
    if (cap < 64) cap = 64;
    for (int c = 0; c < NUM_RINGS; ++c) {
        if (static_cast<int>(rings_[c].size()) != cap) {
            rings_[c].assign(static_cast<size_t>(cap), 0.0f);
            writePos_[c] = 0;
        }
        ringCap_[c] = cap;
    }
}

void AcousticalSpatialProcessor::releaseResources() {}

void AcousticalSpatialProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin(NUM_RINGS, buffer.getNumChannels());
    if (numSamples <= 0 || numCh <= 0) return;

    // Una sola lectura de parámetros por bloque (atómicos del APVTS) y una
    // sola llamada a la ley por bloque: igual que el VST principal.
    const float x = pX->load(std::memory_order_relaxed);
    const float y = pY->load(std::memory_order_relaxed);
    const float z = pZ->load(std::memory_order_relaxed);
    const float size = pSize->load(std::memory_order_relaxed);
    float gainDb = 0.0f, delayMs = 0.0f;
    acoustical_spatial_compensate(x, y, z, size, &gainDb, &delayMs);
    const float gain = (gainDb != 0.0f) ? std::pow(10.0f, gainDb / 20.0f) : 1.0f;
    const int delaySamples = static_cast<int>(
        std::lround(delayMs / 1000.0 * sampleRate_));

    for (int ch = 0; ch < numCh; ++ch) {
        float* data = buffer.getWritePointer(ch);
        std::vector<float>& ring = rings_[ch];
        const int cap = ringCap_[ch];
        int w = writePos_[ch];
        for (int s = 0; s < numSamples; ++s) {
            ring[w] = data[s];
            // Retardo relativo: leo lo que entró `delaySamples` antes (el
            // anillo guarda la entrada cruda; la ganancia se aplica a la
            // salida, igual que en el VST principal).
            int r = w - delaySamples;
            while (r < 0) r += cap;
            data[s] = ring[r] * gain;
            if (++w >= cap) w = 0;
        }
        writePos_[ch] = w;
    }
}

void AcousticalSpatialProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // Mismo patrón que el VST principal: la copa es un ValueTree (JUCE 7) y
    // se serializa directamente a XML en el binario del estado.
    auto xml = apvts.copyState().createXml();
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void AcousticalSpatialProcessor::setStateInformation(const void* data,
                                                       int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
            apvts.replaceState(tree);
        }
    }
}

juce::AudioProcessorEditor* AcousticalSpatialProcessor::createEditor() {
    return new AcousticalSpatialEditor(*this);
}

// Punto de entrada de los wrappers VST3/VST2 generados por juce_add_plugin
// (misma firma que la del VST principal, en PluginProcessor.cpp): sin esta
// función los dll del complementario no enlazan (error LNK2001 en la CI).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AcousticalSpatialProcessor();
}
