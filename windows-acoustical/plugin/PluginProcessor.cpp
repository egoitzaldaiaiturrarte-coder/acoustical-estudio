#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

using juce::String;
// utf8() viene del anonymous namespace de PluginEditor.h (incluido arriba):
// etiquetas en español seguras para MSVC. No redefinirla aquí (C2084).

// === Parámetros: inventario completo del móvil (AudioModels.kt +
// SweeperProcessor.kt) más el generador de referencia ===
juce::AudioProcessorValueTreeState::ParameterLayout
AcousticalAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Motor ---
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"correction", 1}, utf8("Corrección"), true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"maxGain", 1}, utf8("Ganancia máxima (dB)"),
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.1f), 12.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"smoothing", 1}, "Suavizado",
        juce::NormalisableRange<float>(0.05f, 0.8f, 0.01f), 0.3f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"noiseSub", 1}, utf8("Sustracción de ruido"), true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"link", 1}, "Canales L/R enlazados", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"targetSpl", 1}, utf8("SPL objetivo (dB)"),
        juce::NormalisableRange<float>(40.0f, 100.0f, 0.5f), 75.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delayMs", 1}, utf8("Retardo global (ms)"),
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"analysisInterval", 1}, utf8("Intervalo de análisis"),
        juce::StringArray{"25 ms", "50 ms", "100 ms", "200 ms", "500 ms"}, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"bandCount", 1}, utf8("Número de bandas"),
        juce::StringArray{"8", "10", "16", "31 (1/3 oct)", "124 (Ultra)"}, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"fftSize", 1}, utf8("Tamaño de FFT"),
        juce::StringArray{"512", "1024", "2048", "4096", "8192"}, 2));

    // --- Generador de referencia (señal conocida para capturar la referencia) ---
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"genSource", 1}, "Generador",
        juce::StringArray{utf8("Apagado"), utf8("Ruido rosa"),
                          utf8("Ruido blanco"), "Barrido", "Seno"},
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"genLevel", 1}, utf8("Nivel del generador (dB)"),
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -12.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"genFreq", 1}, utf8("Frecuencia del seno (Hz)"),
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 1000.0f));

    // --- Los tres ecuas dinámicos ---
    const float defaultSupportFreqs[4] = {250.0f, 1000.0f, 4000.0f, 12000.0f};
    for (int i = 1; i <= 3; ++i) {
        const String n(i);
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"eq" + n + "Enabled", 1}, "Ecu " + n + " activo", true));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"eq" + n + "Interval", 1},
            utf8("Ecu ") + n + utf8(" intervalo (ms)"), 100, 2000, 800));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Gain", 1}, utf8("Ecu ") + n + utf8(" ganancia (dB)"),
            juce::NormalisableRange<float>(1.0f, 50.0f, 0.1f), 12.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Mixer", 1}, utf8("Ecu ") + n + " mezclador",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"eq" + n + "Speed", 1}, utf8("Ecu ") + n + " velocidad",
            juce::NormalisableRange<float>(0.5f, 4.0f, 0.1f), 1.0f));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{"eq" + n + "Extras", 1},
            utf8("Ecu ") + n + utf8(" barridos extra"), 0, 6, 1));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"eq" + n + "Start", 1}, utf8("Ecu ") + n + " arranque",
            juce::StringArray{utf8("Donde más se necesita"),
                              utf8("De graves a agudos"),
                              utf8("De agudos a graves")},
            i - 1));
        // Bandas de apoyo de frecuencia libre (4 por ecu, como en el móvil)
        for (int k = 1; k <= 4; ++k) {
            const String s(k);
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{"eq" + n + "Sb" + s + "Freq", 1},
                utf8("Ecu ") + n + utf8(" apoyo ") + s + " (Hz)",
                juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f),
                defaultSupportFreqs[k - 1]));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{"eq" + n + "Sb" + s + "Gain", 1},
                utf8("Ecu ") + n + utf8(" apoyo ") + s + " (dB)",
                juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{"eq" + n + "Sb" + s + "Q", 1},
                utf8("Ecu ") + n + utf8(" apoyo ") + s + " Q",
                juce::NormalisableRange<float>(0.5f, 8.0f, 0.1f), 2.0f));
        }
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
    // El análisis lo publica el hilo del motor: se registra UNA vez aquí.
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
    auto l = [this](const String& id) { return apvts.getRawParameterValue(id)->load(); };

    // configure() es thread-safe (stateMutex_ en el motor) y decide solo si la
    // cambio es estructural (recrea FFT/corrector/ecuas) o ligera (actualiza
    // parámetros en sitio sin perder el estado de corrección): se puede llamar
    // con el motor en marcha, sin detenerlo.
    auto c = engine_.config();
    c.correctionEnabled = l("correction") > 0.5f;
    c.maxGainDb = l("maxGain");
    c.smoothingFactor = l("smoothing");
    c.noiseSubtractionEnabled = l("noiseSub") > 0.5f;
    c.targetSpl = l("targetSpl");
    c.analysisInterval = static_cast<acoustical::AnalysisInterval>(
        static_cast<int>(l("analysisInterval")));
    c.bandCount = static_cast<acoustical::BandCount>(static_cast<int>(l("bandCount")));
    c.fftSize = static_cast<acoustical::FftSize>(static_cast<int>(l("fftSize")));
    engine_.configure(c);

    engine_.setEqChannelLinked(l("link") > 0.5f);
    for (int i = 0; i < 3; ++i) {
        const String n(i + 1);
        engine_.setDynamicEqEnabled(i, l("eq" + n + "Enabled") > 0.5f);
        acoustical::DynamicEqConfig cfg;
        cfg.decisionIntervalMs = static_cast<int>(l("eq" + n + "Interval"));
        cfg.maxGainDb = l("eq" + n + "Gain");
        cfg.mixerLevel = l("eq" + n + "Mixer");
        cfg.speedMultiplier = l("eq" + n + "Speed");
        cfg.extraSweeps = static_cast<int>(l("eq" + n + "Extras"));
        cfg.startFrom = static_cast<acoustical::SweepDirection>(
            static_cast<int>(l("eq" + n + "Start")));
        cfg.clamp();
        engine_.setDynamicEqConfig(i, cfg);

        std::vector<acoustical::SupportBand> support;
        for (int k = 1; k <= 4; ++k) {
            const String s(k);
            acoustical::SupportBand band;
            band.frequencyHz = l("eq" + n + "Sb" + s + "Freq");
            band.gainDb = l("eq" + n + "Sb" + s + "Gain");
            band.q = l("eq" + n + "Sb" + s + "Q");
            support.push_back(band);
        }
        engine_.setSupportBands(i, support);
    }
}

void AcousticalAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    // prepareToPlay corre en el HILO de audio: no se puede llamar a
    // configure() directamente aquí — toma stateMutex_, que el hilo del motor
    // sostiene DURANTE la FFT, así que un cambio de rate del host podía
    // bloquear el callback de audio hasta que el motor terminase su tick (ms
    // de xrun). El applyParameters() anterior hacía lo mismo tres veces
    // seguidas bajo el lock estructural.
    //
    // FIX: solo se ENCUELA la config "pedida" (requestConfigure: un lock
    // corto y dedicado del motor, requestMutex_, nunca stateMutex_) y el
    // motor aplica la estructura en su siguiente tick, bajo el lock que ya
    // sostiene (decide estructural vs ligero como siempre).
    //
    // La tasa REAL del dispositivo se re-detecta además en processBlock, que
    // re-prepara el EQ si el bloque llega a otra rate que el anterior: el
    // primer bloque tras el cambio no se procesa con coeficientes de la rate
    // vieja, y el motor re-aplica la curva combinada en su próximo tick.
    auto l = [this](const String& id) { return apvts.getRawParameterValue(id)->load(); };
    acoustical::AudioConfig c;
    c.sampleRate = sampleRate > 87000.0 ? acoustical::SampleRate::Hz96000
                 : sampleRate > 66000.0 ? acoustical::SampleRate::Hz88200
                 : sampleRate > 46000.0 ? acoustical::SampleRate::Hz48000
                                : acoustical::SampleRate::Hz44100;
    c.correctionEnabled = l("correction") > 0.5f;
    c.maxGainDb = l("maxGain");
    c.smoothingFactor = l("smoothing");
    c.noiseSubtractionEnabled = l("noiseSub") > 0.5f;
    c.targetSpl = l("targetSpl");
    c.analysisInterval = static_cast<acoustical::AnalysisInterval>(
        static_cast<int>(l("analysisInterval")));
    c.bandCount = static_cast<acoustical::BandCount>(static_cast<int>(l("bandCount")));
    c.fftSize = static_cast<acoustical::FftSize>(static_cast<int>(l("fftSize")));
    engine_.requestConfigure(c);
    engine_.start();    // no-op si ya estaba en marcha
}

void AcousticalAudioProcessor::releaseResources() {
    // releaseResources corre en el HILO de audio: un stop() desde aquí haría
    // el JOIN del hilo del motor desde el callback (bloquearía hasta que el
    // motor terminase su tick → xrun). FIX: solo se marca la parada
    // solicitada (requestStop: un store atómico, sin join); el join lo hace
    // el destructor del engine (red de seguridad), que corre en el hilo de
    // mensajes al borrar el processor.
    engine_.requestStop();
}

void AcousticalAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;
    const int numOut = getTotalNumOutputChannels();
    const int numIn = getTotalNumInputChannels();
    const float sr = static_cast<float>(getSampleRate());
    auto l = [this](const String& id) { return apvts.getRawParameterValue(id)->load(); };

    // 0. Si el host cambió la sample rate entre bloques (prepareToPlay solo
    //    ENCOLA la nueva config; el motor la aplica en su próximo tick),
    //    re-preparamos el EQ AQUÍ, en el hilo de audio (puede: prepare es un
    //    recomputo de coeficientes que publica un snapshot atómico, no toca
    //    stateMutex_): el primer bloque tras el cambio no se procesa con
    //    coeficientes de la rate vieja (bandas en la frecuencia equivocada).
    //    Se conservan las últimas ganancias (reprepare): el motor re-aplica
    //    la curva combinada en su próximo tick de análisis.
    if (static_cast<int>(sr) != eqPreparedRate_) {
        eqPreparedRate_ = static_cast<int>(sr);
        engine_.eqDspL().reprepare(sr);
        engine_.eqDspR().reprepare(sr);
    }

    // 1. Generador de referencia: sustituye el contenido del canal con una
    //    señal conocida (ruido rosa/blanco, barrido o seno). "Apagado" (0)
    //    deja el input pasar tal cual: la referencia se captura del anillo
    //    pre-EQ (paso 3), así que el generador no debe dejar huella en él.
    const int genSource = static_cast<int>(l("genSource"));
    if (genSource > 0) {
        // Mapeo menú->Waveform EXPLÍCITO: el enum
        // {Sine, BandSine, LogSweep, PinkNoise, WhiteNoise, Silence} no sigue
        // el orden del menú. El `genSource - 1` antiguo sonaba "Ruido rosa"
        // como seno, "Ruido blanco" como seno por banda y "Seno" como ruido
        // rosa, y dejaba a "Apagado" sin efecto una vez se salía de él (el
        // bloque se saltaba pero la última forma de onda seguía rellenando el
        // canal: imposible de apagar desde el menú).
        const auto wave =
            genSource == 1 ? acoustical::SignalGenerator::Waveform::PinkNoise
          : genSource == 2 ? acoustical::SignalGenerator::Waveform::WhiteNoise
          : genSource == 3 ? acoustical::SignalGenerator::Waveform::LogSweep
                           : acoustical::SignalGenerator::Waveform::Sine;
        if (wave != genWaveform_) {
            genWaveform_ = wave;
            generator_.setWaveform(wave);
            generator_.setSweepRange(20.0f, 20000.0f, 5.0f);
        }
        const float freq = l("genFreq");
        const float level = l("genLevel");
        if (std::abs(freq - genFrequency_) > 0.01f) {
            genFrequency_ = freq;
            generator_.setFrequency(freq);
        }
        if (std::abs(level - genLevelDb_) > 0.01f) {
            genLevelDb_ = level;
            generator_.setLevelDb(level);
        }
        if (genBuf_.size() < static_cast<size_t>(numSamples)) genBuf_.resize(numSamples);
        generator_.fill(genBuf_.data(), numSamples, sr);
        for (int ch = 0; ch < numOut; ++ch)
            buffer.copyFrom(ch, 0, genBuf_.data(), numSamples);
    }

    // 2. Retardo global (pasos de 0,01 ms) con latencia reportada al host (PDC)
    const int wantDelay =
        static_cast<int>(std::lround(l("delayMs") * sr / 1000.0f));
    const int minRing = wantDelay + numSamples + 2;
    if (static_cast<int>(delayRing_[0].size()) < minRing) {
        const int newSize = minRing + 512;
        delayRing_[0].assign(static_cast<size_t>(newSize), 0.0f);
        delayRing_[1].assign(static_cast<size_t>(newSize), 0.0f);
        delayWritePos_ = 0;
    }
    const int ringSize = static_cast<int>(delayRing_[0].size());
    delaySamples_ = juce::jlimit(0, juce::jmax(0, ringSize - numSamples - 1), wantDelay);
    // PDC = línea de retardo del usuario + cota del retardo de grupo del banco
    // de biquads (ver kEqGroupDelaySamples en el header: medido sobre el
    // banco real, con margen; sin ella el host compensaría solo el retardo de
    // la línea y la corrección de fase del EQ dejaría un error de
    // alineación).
    const int pdc = delaySamples_ + kEqGroupDelaySamples;
    if (pdc != reportedLatency_) {
        reportedLatency_ = pdc;
        setLatencySamples(pdc);
    }
    if (delaySamples_ > 0) {
        for (int ch = 0; ch < juce::jmin(2, numOut); ++ch) {
            float* data = buffer.getWritePointer(ch);
            auto& ring = delayRing_[static_cast<size_t>(ch)];
            for (int s = 0; s < numSamples; ++s) {
                const int writePos = (delayWritePos_ + s) % ringSize;
                const int readPos = (writePos - delaySamples_ + ringSize) % ringSize;
                const float delayed = ring[static_cast<size_t>(readPos)];
                ring[static_cast<size_t>(writePos)] = data[s];
                data[s] = delayed;
            }
        }
        delayWritePos_ = (delayWritePos_ + numSamples) % ringSize;
    }

    // 3. Copia del espectro ANTES de salir (pre-EQ): de aquí se captura la
    //    referencia al pulsar "Capturar referencia".
    const int mixCount = juce::jmin(2, juce::jmax(numIn, numOut));
    if (monoBuf_.size() < static_cast<size_t>(numSamples)) monoBuf_.resize(numSamples);
    float* mono = monoBuf_.data();
    std::fill_n(mono, numSamples, 0.0f);
    for (int ch = 0; ch < mixCount; ++ch) {
        const float* src = buffer.getReadPointer(
            juce::jmin(ch, juce::jmax(numIn, numOut) - 1));
        for (int s = 0; s < numSamples; ++s) mono[s] += src[s];
    }
    // El thread de audio es el único escritor del anillo: write() avanza la
    // cabeza y, si se llena, descarta lo más antiguo (conservando las CAP
    // últimas) — sin lock ni realloc en el path de audio.
    preRing_.write(mono, numSamples);

    // 4. EQ en tiempo real por canal (corrección principal + los tres ecuas)
    if (leftBuf_.size() < static_cast<size_t>(numSamples)) leftBuf_.resize(numSamples);
    if (rightBuf_.size() < static_cast<size_t>(numSamples)) rightBuf_.resize(numSamples);
    float* left = leftBuf_.data();
    float* right = rightBuf_.data();
    std::copy_n(buffer.getReadPointer(0), numSamples, left);
    std::copy_n(buffer.getReadPointer(numOut > 1 ? 1 : 0), numSamples, right);
    engine_.eqDspL().process(left, numSamples);
    engine_.eqDspR().process(right, numSamples);
    buffer.copyFrom(0, 0, left, numSamples);
    if (numOut > 1) buffer.copyFrom(1, 0, right, numSamples);

    // 5. El espectro DESPUÉS (lo que sale hacia los altavoces) alimenta el
    //    motor: los cuatro procesos lo empujan hacia la referencia capturada.
    if (monoBuf_.size() < static_cast<size_t>(numSamples)) monoBuf_.resize(numSamples);
    std::fill_n(mono, numSamples, 0.0f);
    for (int ch = 0; ch < juce::jmin(2, numOut); ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s) mono[s] += src[s];
    }
    engine_.pushSamples(mono, numSamples, static_cast<int>(sr));
}

void AcousticalAudioProcessor::captureReferenceFromInput() {
    // El reader es la captura del engine (hilo de mensajes): readLatest solo
    // lee los punteros atómicos (w, r) y copia las últimas muestras — sin
    // lock, sin realloc; entrega toda la ventana reciente, como el deque.
    std::vector<float> block;
    preRing_.readLatest(block);
    engine_.captureReferenceFrom(block, static_cast<int>(getSampleRate()));
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
