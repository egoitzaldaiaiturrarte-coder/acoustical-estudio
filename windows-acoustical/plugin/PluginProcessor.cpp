#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "plugin_genmap.h"

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
        juce::NormalisableRange<float>(30.0f, 110.0f, 0.5f), 75.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"delayMs", 1}, utf8("Retardo global (ms)"),
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f));

    // --- Espacio (v1.8): posición del elemento que se escucha ---
    // Ganancia (20·log10 distancia + tamaño + off-axis) y retardo de
    // propagación según la posición: la MISMA ley que la app Windows y la app
    // móvil (core C, acoustical_spatial_compensate), aplicada a la salida del
    // plugin (ganancia post-EQ + el retardo suma a la línea global).
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

    // --- Modo de trabajo (v1.8): perfil completo del catálogo compartido ---
    // 0 = "Actual" (ajustes libres, sin modo); 1..6 = los perfiles del core
    // C (Coche Normal / Aire libre, Estudio-Esc 1, Escenarios 2/3, PAs):
    // elegir uno reescribe el resto de parámetros (applyParameters), y al
    // arrancar el plugin el estado guardado se restaura por el APVTS.
    {
        juce::StringArray names;
        names.add(utf8("Actual (sin modo)"));
        const auto* profiles = acoustical_mode_profiles();
        for (int i = 0; i < ACOUSTICAL_MODE_COUNT; ++i)
            names.add(utf8(profiles[i].name));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"mode", 1}, utf8("Modo de trabajo"), names, 0));
    }
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
    // v1.9: el menú unificado de las tres versiones (dsp/plugin_genmap.h,
    // testeado por el gate DSP): "Silencio" es la PRIMERA opción y la única
    // que nunca suena (ceros en la salida); "Apagado" es el passthrough
    // propio del VST (la entrada pasa tal cual). El estado guardado con el
    // menú antiguo de 5 opciones (v1.8) se migra en setStateInformation
    // (marcador "genSourceMenu"), de modo que cada proyecto sigue sonando
    // exactamente lo que sonaba.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"genSource", 1}, "Generador",
        juce::StringArray{utf8("Silencio"), utf8("Apagado"), utf8("Ruido rosa"),
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

    // === Modo de trabajo (v1.8) ===
    // El catálogo (core C: la MISMA tabla que la app Windows y el móvil) elige
    // la opción "mode"; cuando cambia, aquí se REESCRIBEN el resto de
    // parámetros con el perfil completo del modo (motor + 3 ecuas con sus
    // bandas de apoyo + posición espacial). La opción 0 ("Actual") no escribe
    // nada: los parámetros quedan libres. appliedMode_ guarda el último modo
    // aplicado: sin esa guarda, la escritura de cada parámetro dispararía su
    // propio callback async y el modo se re-aplicaría en bucle.
    const int modeSel = static_cast<int>(l("mode"));
    if (modeSel != appliedMode_) {
        appliedMode_ = modeSel;
        if (modeSel >= 1 && modeSel <= ACOUSTICAL_MODE_COUNT) {
            const auto* m = &acoustical_mode_profiles()[modeSel - 1];
            auto setRaw = [this](const String& id, float v) {
                if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(v);
            };
            // choice del APVTS en JUCE 7: valor crudo = índice 0-based
            auto setChoice = [this](const String& id, int index, int count) {
                if (auto* p = apvts.getParameter(id))
                    p->setValueNotifyingHost(
                        juce::NormalisableRange<float>(0.0f,
                            static_cast<float>(count - 1))
                            .convertTo0to1(static_cast<float>(index)));
            };
            setRaw("targetSpl", m->targetSpl);
            setRaw("maxGain", m->maxGainDb);
            setRaw("smoothing", m->smoothingFactor);
            setRaw("noiseSub", m->noiseSubtractionEnabled ? 1.0f : 0.0f);
            setRaw("correction", m->correctionEnabled ? 1.0f : 0.0f);
            setChoice("analysisInterval", m->analysisIntervalIndex, 5);
            setChoice("bandCount", m->bandCountIndex, 5);
            setChoice("fftSize", m->fftSizeIndex, 5);
            for (int i = 0; i < 3; ++i) {
                const String n(i + 1);
                const auto& e = m->eqs[i];
                setRaw("eq" + n + "Enabled", e.enabled ? 1.0f : 0.0f);
                setRaw("eq" + n + "Interval", static_cast<float>(e.intervalMs));
                setRaw("eq" + n + "Gain", e.maxGainDb);
                setRaw("eq" + n + "Mixer", e.mixerLevel);
                setRaw("eq" + n + "Speed", e.speedMultiplier);
                setRaw("eq" + n + "Extras", static_cast<float>(e.extraSweeps));
                setChoice("eq" + n + "Start", e.startFrom, 3);
                for (int k = 1; k <= 4; ++k) {
                    const String s(k);
                    setRaw("eq" + n + "Sb" + s + "Freq", e.support[k - 1].frequencyHz);
                    setRaw("eq" + n + "Sb" + s + "Gain", e.support[k - 1].gainDb);
                    setRaw("eq" + n + "Sb" + s + "Q", e.support[k - 1].q);
                }
            }
            setRaw("spatialX", m->spatialX);
            setRaw("spatialY", m->spatialY);
            setRaw("spatialZ", m->spatialZ);
            setRaw("spatialSize", m->spatialSize);
        }
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
    //    señal conocida. El anillo pre-EQ del paso 3 se alimenta de este
    //    buffer, así que lo que la referencia captura es EXACTAMENTE lo que
    //    hay en el menú:
    //      - "Silencio" (la primera opción del menú unificado de las tres
    //        versiones): CEROS — la única que nunca suena; la referencia
    //        capturaría silencio (no deja huella).
    //      - "Apagado" (passthrough, propio del VST): la entrada pasa tal
    //        cual; la referencia se captura de la fuente real.
    //      - Rosa/Blanco/Barrido/Seno: la señal conocida, para capturar una
    //        referencia de laboratorio sin depender de la fuente.
    //
    //    Mapeo menú->forma de onda EXPLÍCITO (dsp/plugin_genmap.h, testeado
    //    por el gate DSP): el valor crudo del parámetro choice es 0..1
    //    (índice/(nº-1)), así que se convierte de vuelta a ÍNDICE con
    //    redondeo — la truncación directa al int que hacía el código anterior
    //    (`static_cast<int>(raw)`) colapsaba las cuatro primeras opciones del
    //    menú a "1 kHz de seno" (crudo 0..0.75 → 0) y dejaba a "Seno"
    //    (crudo 1.0 → 1) sonando "Ruido rosa": el bug que esta versión corrige.
    const int genSource =
        juce::jlimit(0, acoustical::genmenu::kMenuCount - 1,
                      static_cast<int>(std::lround(
                          l("genSource") * static_cast<float>(
                              acoustical::genmenu::kMenuCount - 1))));
    if (genSource != 1) {   // 1 = "Apagado": passthrough (el input pasa tal cual)
        const int wave = acoustical::genmenu::waveformForMenuIndex(genSource);
        if (wave >= 0) {
            // 0 = Silencio (el core rellena con ceros) y 2..5 = rosa/blanco/
            // barrido/seno (valores canónicos ACOUSTICAL_WAVE_*).
            const auto wf = acoustical::SignalGenerator::fromCanonical(wave);
            if (wf != genWaveform_) {
                genWaveform_ = wf;
                generator_.setWaveform(wf);
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
    }

    // 2. Retardo global (pasos de 0,01 ms) con latencia reportada al host (PDC).
    //    El retardo de propagación espacial (v1.8, paso siguiente) se SUMA a la
    //    línea: la posición lejana se alínea con la cercana con la MISMA ley
    //    del core C que usan la app y el móvil, y el host (Cubase) compensa el
    //    total vía PDC.
    const float sx = l("spatialX");
    const float sy = l("spatialY");
    const float sz = l("spatialZ");
    const float ss = l("spatialSize");
    float spatialGainDb = 0.0f, spatialDelayMs = 0.0f;
    acoustical_spatial_compensate(sx, sy, sz, ss, &spatialGainDb, &spatialDelayMs);
    const float spatialGain = (spatialGainDb != 0.0f)
        ? std::pow(10.0f, spatialGainDb / 20.0f) : 1.0f;
    const int wantDelay =
        static_cast<int>(std::lround((l("delayMs") + spatialDelayMs) * sr / 1000.0f));
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
    //    Se toma ANTES de la ganancia espacial (paso 6): la compensación es
    //    intencional y el bucle de corrección no debe tratarla de errar.
    if (monoBuf_.size() < static_cast<size_t>(numSamples)) monoBuf_.resize(numSamples);
    std::fill_n(mono, numSamples, 0.0f);
    for (int ch = 0; ch < juce::jmin(2, numOut); ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s) mono[s] += src[s];
    }
    engine_.pushSamples(mono, numSamples, static_cast<int>(sr));

    // 6. Ganancia espacial (v1.8): la mitad de la compensación (la ganancia;
    //    el retardo ya viajó por la línea del paso 2) se aplica a la salida
    //    final, post-EQ: "lejos y grande" llega con el mismo nivel que
    //    "cerca y pequeño" en cualquier fuente (generador incluido).
    if (spatialGain != 1.0f) {
        for (int ch = 0; ch < juce::jmin(2, numOut); ++ch) {
            float* data = buffer.getWritePointer(ch);
            for (int s = 0; s < numSamples; ++s) data[s] *= spatialGain;
        }
    }
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
    // v1.9: el marcador "genSourceMenu" dice que este estado es del menú
    // generador de 6 opciones. Se escribe en CADA guardado, así el primer
    // save tras la migración cierra el bucle: la migración (abajo) es
    // one-shot por proyecto.
    //
    // JUCE 7: la copa se maneja directamente como ValueTree. La versión
    // original pasaba por juce::Var, que JUCE 7 ya no conecta con
    // ValueTree (la conversión y el setProperty con bool skip-undo
    // desaparecieron: ahora se pasa UndoManager*, nullptr si no interesa).
    auto state = apvts.copyState();
    if (state.getProperty("genSourceMenu").toString().trim() != "6")
        state.setProperty("genSourceMenu", "6", nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AcousticalAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
            // Migración one-shot del menú antiguo (v1.8, 5 opciones, ver
            // dsp/plugin_genmap.h): solo se toca un estado SIN marcador (de
            // antes del menú nuevo) y desplaza cada crudo k/4 a su posición
            // (k+1)/5 en el menú nuevo: Apagado→Apagado, Rosa→Rosa,
            // Blanco→Blanco, Barrido→Barrido, Seno→Seno (1.0 en ambos). El
            // proyecto sigue sonando exactamente lo que sonaba.
            if (tree.getProperty("genSourceMenu").toString().trim() != "6") {
                for (auto child : tree) {
                    if (child.getProperty("id").toString() == "genSource") {
                        // JUCE 7: el var ya no tiene toDouble(); la
                        // conversión numérica va por sus operadores
                        // de conversión implicitos.
                        const float raw =
                            static_cast<float>(child.getProperty("value"));
                        const float migrated = acoustical::genmenu::migrateRawValue(raw);
                        if (migrated != raw)
                            child.setProperty("value", migrated, nullptr);
                        break;
                    }
                }
            }
            tree.setProperty("genSourceMenu", "6", nullptr);
            apvts.replaceState(tree);
            applyParameters();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AcousticalAudioProcessor();
}
