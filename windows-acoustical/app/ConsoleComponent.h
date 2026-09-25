// ConsoleComponent.h — consola principal: pestañas EQ / Ruteos / Ajustes /
// Análisis, glue del motor con el dispositivo de audio, generador de señales,
// presets, atajos de teclado y enlace con el móvil.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <AcousticalEngine.h>
#include "Theme.h"
#include "EqCanvas.h"
#include "DynamicEqCard.h"
#include "SpectrumView.h"
#include "RoutingMatrix.h"
#include "SettingsPanel.h"
#include "RouteHub.h"
#include "HubPanel.h"
#include "PhoneLink.h"
#include "AsioBridgeClient.h"

class ConsoleComponent : public juce::Component,
                         private juce::AudioIODeviceCallback,
                         private juce::Timer {
public:
    // === Pestaña "Audio": panel de dispositivos + puente ASIO (Cubase) ===
    class AudioTab : public juce::Component {
    public:
        AudioTab(juce::AudioDeviceSelectorComponent& panel, AsioBridgeClient& bridge)
            : panel_(panel), bridge_(bridge) {
            addAndMakeVisible(panel_);
            addAndMakeVisible(bridgeButton_);
            bridgeButton_.setButtonText("Puent ASIO (Cubase)");
            bridgeButton_.setClickingTogglesState(true);
            bridgeButton_.onClick = [this] {
                if (bridgeButton_.getToggleState())
                    bridgeOk_ = bridge_.connect();
                else
                    bridge_.disconnect();
                updateStatus();
            };
            addAndMakeVisible(bridgeStatus_);
            bridgeStatus_.setFont(juce::Font(12.0f));
            bridgeStatus_.setColour(juce::Label::textColourId, theme::textDim);
            updateStatus();
        }

        void resized() override {
            auto b = getLocalBounds().reduced(10);
            bridgeButton_.setBounds(b.removeFromTop(34).removeFromLeft(220));
            bridgeStatus_.setBounds(b.removeFromTop(26));
            panel_.setBounds(b.reduced(0, 2));
        }

        void refreshStatus() { if (bridge_.isConnected()) updateStatus(); }

    private:
        void updateStatus() {
            juce::String s;
            if (!bridge_.isConnected())
                s = bridgeOk_
                    ? "Puent desconectado."
                    : juce::String::fromUTF8("Puent ASIO apagado. Ruta: maestro de Cubase → \"Bridge Out\" (suena por la "
                                             "tarjeta con el EQ aplicado); \"Bridge In\" → señal de prueba hacia Cubase.");
            else
                s = juce::String::fromUTF8("Puent ASIO conectado · ")
                    + juce::String(bridge_.sampleRate())
                    + juce::String::fromUTF8(" Hz · buffer ")
                    + juce::String(bridge_.bufferSize())
                    + juce::String::fromUTF8(" — el audio de Cubase pasa por el EQ a "
                                             "la tarjeta real; la señal de prueba sale "
                                             "hacia Cubase.");
            bridgeStatus_.setText(s, juce::dontSendNotification);
        }

        juce::AudioDeviceSelectorComponent& panel_;
        AsioBridgeClient& bridge_;
        juce::ToggleButton bridgeButton_;
        juce::Label bridgeStatus_;
        bool bridgeOk_ = false;
    };

    ConsoleComponent(juce::AudioDeviceManager& dm, acoustical::AcousticalEngine& eng)
        : deviceManager_(dm), engine_(eng) {
        theme::apply(*this);

        // === Barra superior: transport + presets + estado del móvil ===
        addAndMakeVisible(powerButton_);
        powerButton_.setButtonText("Motor");
        powerButton_.setColour(juce::TextButton::buttonColourId, theme::primaryDark);
        powerButton_.setTooltip(juce::String::fromUTF8("Arrancar o parar el motor de audio"));
        powerButton_.onClick = [this] { toggleEngine(); };

        addAndMakeVisible(referenceButton_);
        referenceButton_.setButtonText("Referencia");
        referenceButton_.setTooltip(juce::String::fromUTF8("Capturar la referencia de la sala"));
        referenceButton_.onClick = [this] { engine_.captureReference(); };

        addAndMakeVisible(noiseButton_);
        noiseButton_.setButtonText("Ruido");
        noiseButton_.setTooltip(juce::String::fromUTF8("Capturar el ruido de fondo"));
        noiseButton_.onClick = [this] { engine_.startNoiseCapture(); };

        addAndMakeVisible(freezeButton_);
        freezeButton_.setButtonText("Congelar");
        freezeButton_.setClickingTogglesState(true);
        freezeButton_.setColour(juce::TextButton::buttonOnColourId, theme::eq2Amber);
        freezeButton_.setTooltip(juce::String::fromUTF8("Congelar la pantalla (barra espaciadora); el audio sigue corrigiendo"));

        addAndMakeVisible(presetBox_);
        presetBox_.setTextWhenNothingSelected("Preset");
        presetBox_.setTooltip(juce::String::fromUTF8("Cargar un preset guardado"));
        presetBox_.onChange = [this] { loadSelectedPreset(); };

        addAndMakeVisible(savePresetButton_);
        savePresetButton_.setButtonText("Guardar");
        savePresetButton_.setTooltip(juce::String::fromUTF8("Guardar el preset actual"));
        savePresetButton_.onClick = [this] { saveCurrentPreset(); };

        addAndMakeVisible(syncButton_);
        syncButton_.setButtonText("Sincronizar");
        syncButton_.setTooltip(juce::String::fromUTF8("Sincronizar con el móvil (Wi-Fi / USB)"));
        // "Sincronizar" = el PC envía su configuración completa al móvil y
        // procesa la respuesta (config del móvil en vivo + comandos de Hub)
        // en el mismo intercambio.
        syncButton_.onClick = [this] {
            if (!phoneLink_) return;
            auto* pl = new juce::DynamicObject();
            pl->setProperty("config", serializeEngine());
            juce::var reply;
            if (phoneLink_->pushSync(juce::var(pl), juce::String::fromUTF8("Sincronizado con el móvil"), &reply))
                applyPhoneSync(reply, false);
        };

        addAndMakeVisible(phoneLabel_);
        phoneLabel_.setFont(juce::Font(12.0f));
        phoneLabel_.setColour(juce::Label::textColourId, theme::textDim);

        // === Selector de vista (los mismos nombres que las pestañas) ===
        addAndMakeVisible(viewBox_);
        viewBox_.addItem(juce::String::fromUTF8("1 · EQ"), 1);
        viewBox_.addItem(juce::String::fromUTF8("2 · Ecuas dinámicos"), 2);
        viewBox_.addItem(juce::String::fromUTF8("3 · Hub"), 3);
        viewBox_.addItem(juce::String::fromUTF8("4 · Ruteos"), 4);
        viewBox_.addItem(juce::String::fromUTF8("5 · Ajustes"), 5);
        viewBox_.addItem(juce::String::fromUTF8("6 · Análisis"), 6);
        viewBox_.addItem(juce::String::fromUTF8("7 · Audio"), 7);
        viewBox_.setSelectedId(1, juce::dontSendNotification);
        viewBox_.onChange = [this] {
            tabs_->setCurrentTabIndex(viewBox_.getSelectedId() - 1, true);
        };

        addAndMakeVisible(fullscreenButton_);
        fullscreenButton_.setButtonText("Pantalla (F11)");
        fullscreenButton_.setClickingTogglesState(true);
        fullscreenButton_.setTooltip(juce::String::fromUTF8("Pantalla completa (F11)"));
        fullscreenButton_.onClick = [this] { toggleFullscreen(); };

        // === Panel de audio: todas las tarjetas del PC con sus ajustes ===
        audioPanel_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager_, 0, 16, 0, 16, false, false, true, false);
        audioPanel_->setItemHeight(36);
        audioTab_ = std::make_unique<AudioTab>(*audioPanel_, bridge_);

        addAndMakeVisible(lockButton_);
        lockButton_.setButtonText("Bloquear");
        lockButton_.setClickingTogglesState(true);
        lockButton_.setColour(juce::TextButton::buttonOnColourId, theme::eq2Amber);
        lockButton_.setTooltip(juce::String::fromUTF8("Bloquear los faders (evita tocarlos sin querer)"));
        lockButton_.onClick = [this] { fadersLocked_ = lockButton_.getToggleState(); };

        addAndMakeVisible(undoButton_);
        undoButton_.setButtonText("Desh.");
        undoButton_.setTooltip("Deshacer");
        undoButton_.onClick = [this] { undo(); };

        addAndMakeVisible(redoButton_);
        redoButton_.setButtonText("Reh.");
        redoButton_.setTooltip("Rehacer");
        redoButton_.onClick = [this] { redo(); };

        // === Los tres ecuas dinámicos, idénticos y seguidos ===
        for (int i = 0; i < 3; ++i) {
            DynamicEqCard::Snapshot snap;
            snap.title = juce::String::fromUTF8("Ecu dinámico ") + juce::String(i + 1);
            snap.directionLabel = i == 0 ? juce::String::fromUTF8("Donde más se necesita")
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            snap.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            cards_[i] = std::make_unique<DynamicEqCard>(snap);
            eqCardsPanel_.addAndMakeVisible(*cards_[i]);
        }
        addAndMakeVisible(eqCardsPanel_);

        // === EQ principal ===
        eqCanvas_ = std::make_unique<EqCanvas>([this](int band, float gain) {
            if (fadersLocked_) return;
            engine_.setBandGain(band, gain);
        });
        eqCanvas_->onBandEditStart = [this] { if (!fadersLocked_) pushUndo(); };
        addAndMakeVisible(*eqCanvas_);

        // === Pestañas ===
        // Enlace con el móvil (Wi-Fi con respaldo USB): se crea antes que el
        // panel de Ajustes porque este muestra su estado y el emparejamiento
        auto adb = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getParentDirectory().getChildFile("adb/adb.exe");
        phoneLink_ = std::make_unique<PhoneLink>(adb);
        phoneLink_->onSync = [this](const juce::var& payload) { applyPhoneSync(payload); };
        phoneLink_->startWatchdog();

        spectrumView_ = std::make_unique<SpectrumView>();
        routingMatrix_ = std::make_unique<RoutingMatrix>(deviceManager_);
        loadSavedSettings();  // últimos valores funcionales
        settingsPanel_ = std::make_unique<SettingsPanel>(engine_, phoneLink_.get(), [this](bool active) {
            generatorActive_.store(active);
        });
        settingsPanel_->onBeforeChange = [this] { pushUndo(); };
        settingsPanel_->onAfterChange = [this] { saveSettings(); };
        settingsPanel_->onReceiveFromPhone = [this] {
            if (!phoneLink_) return;
            juce::var reply;
            if (phoneLink_->requestSync(&reply)) {
                applyPhoneSync(reply, true);
                phoneLink_->setStatus(juce::String::fromUTF8("Ajustes recibidos del móvil"));
            }
        };
        hub_ = std::make_unique<RouteHub>(deviceManager_);
        hubPanel_ = std::make_unique<HubPanel>(*hub_);
        tabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs_->addTab("EQ", theme::surface, eqCanvas_.get(), false);
        tabs_->addTab(juce::String::fromUTF8("Ecuas dinámicos"), theme::surface, &eqCardsPanel_, false);
        tabs_->addTab("Hub", theme::surface, hubPanel_.get(), false);
        tabs_->addTab("Ruteos", theme::surface, routingMatrix_.get(), false);
        tabs_->addTab("Ajustes", theme::surface, settingsPanel_.get(), false);
        tabs_->addTab(juce::String::fromUTF8("Análisis"), theme::surface, spectrumView_.get(), false);
        tabs_->addTab("Audio", theme::surface, audioTab_.get(), false);
        addAndMakeVisible(*tabs_);

        refreshPresets();
        engine_.onAnalysis = [this](const acoustical::AnalysisResult& r) { storeAnalysis(r); };
        engine_.onSweep = [this](acoustical::SweepProcess p, const acoustical::SweepStep& s) {
            storeSweep(p, s);
        };

        startTimerHz(30);
        deviceManager_.addAudioCallback(this);

        // Motor arrancado por defecto: solo hay que tener el audio seleccionado
        if (engine_.start()) powerButton_.setButtonText("Parar");
    }

    // Control del generador de señales (desde Ajustes)
    std::atomic<bool> generatorActive_{false};

    ~ConsoleComponent() override {
        phoneLink_->stopWatchdog();
        deviceManager_.removeAudioCallback(this);
        engine_.stop();
    }

    // === Atajos de teclado ===
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::F1Key) { toggleDynamicEq(0); return true; }
        if (key == juce::KeyPress::F2Key) { toggleDynamicEq(1); return true; }
        if (key == juce::KeyPress::F3Key) { toggleDynamicEq(2); return true; }
        if (key == juce::KeyPress::spaceKey) { freezeButton_.triggerClick(); return true; }
        if (key == juce::KeyPress::F11Key) { toggleFullscreen(); return true; }
        // Ctrl+Z deshacer · Ctrl+Shift+Z / Ctrl+Y rehacer
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown()) {
            const auto kc = key.getKeyCode();
            if (kc == 'Z') { mods.isShiftDown() ? redo() : undo(); return true; }
            if (kc == 'Y') { redo(); return true; }
        }
        // Teclas 1-7: saltar directo a cada vista
        const auto ch = key.getTextCharacter();
        if (ch >= '1' && ch <= '7') {
            tabs_->setCurrentTabIndex(ch - '1', true);
            return true;
        }
        return false;
    }

private:
    // === Motor / audio ===

    void toggleEngine() {
        if (engine_.isRunning()) {
            engine_.stop();
            powerButton_.setButtonText("Motor");
        } else {
            engine_.start();
            powerButton_.setButtonText("Parar");
        }
    }

    void toggleDynamicEq(int index) {
        const bool target = !dynamicEqEnabled_[index];
        engine_.setDynamicEqEnabled(index, target);
        dynamicEqEnabled_[index] = target;
    }

    void toggleFullscreen() {
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) {
            const bool target = !w->isFullScreen();
            w->setFullScreen(target);
            fullscreenButton_.setToggleState(target, juce::dontSendNotification);
        }
    }

    // === Deshacer / rehacer de la configuración ===

    void pushUndo() {
        undoStack_.push_back(engine_.config());
        if (undoStack_.size() > 100) undoStack_.pop_front();
        redoStack_.clear();
    }

    void undo() {
        if (undoStack_.empty()) return;
        redoStack_.push_back(engine_.config());
        engine_.configure(undoStack_.back());
        undoStack_.pop_back();
        settingsPanel_->refresh();
    }

    void redo() {
        if (redoStack_.empty()) return;
        undoStack_.push_back(engine_.config());
        engine_.configure(redoStack_.back());
        redoStack_.pop_back();
        settingsPanel_->refresh();
    }

    // === Guardado automático de los últimos valores funcionales ===

    juce::File settingsFile() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("settings.json");
    }

    void saveSettings() {
        settingsFile().replaceWithText(juce::JSON::toString(serializeEngine(), true));
    }

    void loadSavedSettings() {
        const auto f = settingsFile();
        if (!f.existsAsFile()) return;
        const auto v = juce::JSON::parse(f);
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        auto num = [obj](const char* key, double def) {
            const auto val = obj->getProperty(key);
            return val.isVoid() ? def : static_cast<double>(val);
        };
        auto flag = [obj](const char* key, bool def) {
            const auto val = obj->getProperty(key);
            return val.isVoid() ? def : static_cast<bool>(val);
        };
        auto c = engine_.config();
        c.maxGainDb = static_cast<float>(num("maxGainDb", c.maxGainDb));
        c.smoothingFactor = static_cast<float>(num("smoothingFactor", c.smoothingFactor));
        c.noiseFloorDb = static_cast<float>(num("noiseFloorDb", c.noiseFloorDb));
        c.noiseSubtractionEnabled = flag("noiseSubtractionEnabled", c.noiseSubtractionEnabled);
        c.correctionEnabled = flag("correctionEnabled", c.correctionEnabled);
        c.targetSpl = static_cast<float>(num("targetSpl", c.targetSpl));
        c.audioDelayMs = static_cast<float>(num("audioDelayMs", c.audioDelayMs));
        engine_.configure(c);
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int numInputs,
                                          float* const* output, int numOutputs,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
        // Niveles de entrada (Ruteos > Entradas): ganancia por canal,
        // atómica (escrita por la UI, leída aquí) y aplicada antes del motor.
        const float inL = routingMatrix_->inputGainL();
        const float inR = routingMatrix_->inputGainR();
        for (int ch = 0; ch < numInputs; ++ch)
            if (const auto* in = input[ch]) {
                const float g = ch == 0 ? inL : (ch == 1 ? inR : 1.0f);
                for (int s = 0; s < numSamples; ++s) mono[s] += in[s] * g;
            }

        // Generador de señales: es con estado de fase, se genera UNA vez por
        // bloque y se comparte entre la salida de la tarjeta real y el
        // puente ASIO (antes se re-asignaba el buffer en cada bloque, A4).
        int genFrames = 0;
        if (generatorActive_.load(std::memory_order_relaxed)) {
            if (genBuf_.size() < static_cast<size_t>(numSamples)) genBuf_.resize(numSamples);
            engine_.signalGenerator().fill(genBuf_.data(), numSamples,
                                           static_cast<float>(lastSampleRate_));
            genFrames = numSamples;
            if (numOutputs > 0)
                for (int s = 0; s < numSamples; ++s) mono[s] += genBuf_[s];
        }

        // Puente ASIO (C5): el audio de Cubase (ruteado a "Bridge Out") entra
        // como una entrada más: pasa por el análisis y por el EQ, y suena por
        // la tarjeta real.
        int bridgeFrames = 0;
        if (bridge_.isConnected()) {
            if (bridgeBufL_.size() < static_cast<size_t>(numSamples)) {
                bridgeBufL_.resize(numSamples);
                bridgeBufR_.resize(numSamples);
            }
            bridgeFrames = bridge_.pullCapture(bridgeBufL_.data(), bridgeBufR_.data(),
                                               numSamples, lastSampleRate_);
            for (int s = 0; s < bridgeFrames; ++s)
                mono[s] += bridgeBufL_[s] + bridgeBufR_[s];
        }

        engine_.pushSamples(mono.data(), numSamples, lastSampleRate_);

        // Salida: EQ en tiempo real por canal (correcciones de los 3 ecuas)
        std::vector<float> left(static_cast<size_t>(numSamples));
        std::vector<float> right(static_cast<size_t>(numSamples));
        {
            std::lock_guard<std::mutex> lock(dspMutex_);
            std::copy(mono.begin(), mono.end(), left.begin());
            std::copy(mono.begin(), mono.end(), right.begin());
            engine_.eqDspL().process(left.data(), numSamples);
            engine_.eqDspR().process(right.data(), numSamples);
        }

        // Hub del sistema: la ruta principal aplica su fase/retardo/ganancia y
        // las rutas auxiliares reciben la señal post-EQ (multiruta simultánea)
        hub_->setStreamInfo(lastSampleRate_);
        hub_->processMaster(left.data(), right.data(), numSamples, mono.data());

        // Volumen general (pestaña Ruteos): escala solo la salida a la tarjeta
        const float outGain = routingMatrix_->gain();
        if (outGain != 1.0f)
            for (int s = 0; s < numSamples; ++s) {
                left[s] *= outGain;
                right[s] *= outGain;
            }

        if (numOutputs > 0 && output[0])
            std::copy(left.begin(), left.end(), output[0]);
        if (numOutputs > 1 && output[1])
            std::copy(right.begin(), right.end(), output[1]);
        for (int ch = 2; ch < numOutputs; ++ch)
            if (output[ch])
                std::copy(right.begin(), right.end(), output[ch]);

        // Playback del puente: la señal de prueba (o silencio) sale hacia
        // Cubase por el anillo de playback; reutiliza las muestras del
        // generador ya producidas para la tarjeta real.
        if (bridge_.isConnected()) {
            if (genFrames <= 0) {
                if (genBuf_.size() < static_cast<size_t>(numSamples)) genBuf_.resize(numSamples);
                std::fill_n(genBuf_.data(), numSamples, 0.0f);
                genFrames = numSamples;
            }
            bridge_.pushPlayback(genBuf_.data(), genBuf_.data(), genFrames, lastSampleRate_);
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        lastSampleRate_ = device ? device->getCurrentSampleRate() : 48000.0;
    }
    void audioDeviceStopped() override {}

    // === Estado compartido entre el hilo del motor y la UI ===

    void storeAnalysis(const acoustical::AnalysisResult& r) {
        std::lock_guard<std::mutex> lock(analysisMutex_);
        lastAnalysis_ = std::make_unique<acoustical::AnalysisResult>(r);
    }

    void storeSweep(acoustical::SweepProcess p, const acoustical::SweepStep& s) {
        const int i = static_cast<int>(p);
        std::lock_guard<std::mutex> lock(sweepMutex_);
        sweepSteps_[i] = s;
        activeBand_[i] = s.bandIndex;
        activeChannel_[i] = s.channel;
    }

    void timerCallback() override {
        // 0. Estado del puente ASIO (el driver puede cambiar de tasa con Cubase)
        if (bridge_.isConnected()) audioTab_->refreshStatus();
        phoneLabel_.setText(phoneLink_ ? phoneLink_->lastSyncInfo() : juce::String(),
                            juce::dontSendNotification);

        // Congelar (barra espaciadora): la pantalla de análisis queda quieta;
        // el motor de audio y el análisis siguen corriendo.
        if (freezeButton_.getToggleState())
            return;

        // 1. EQ principal con resaltados por ecu dinámico
        EqCanvas::Snapshot snap;
        {
            std::lock_guard<std::mutex> lock(analysisMutex_);
            if (!lastAnalysis_) return;
            snap.bandFrequencies = engine_.bandFrequencies();
            snap.gains = lastAnalysis_->combinedGainsL;
            snap.gainsR = lastAnalysis_->combinedGainsR;
            snap.maxGainDb = std::max(engine_.config().maxGainDb,
                                      acoustical::DynamicEqConfig::MAX_GAIN_DB);
            snap.stereoUnlinked = false;
            for (int i = 0; i < 3; ++i) {
                if (!dynamicEqEnabled_[i]) continue;
                const int b = activeBand_[i];
                if (b >= 0 && b < static_cast<int>(snap.bandFrequencies.size()))
                    snap.highlights[b] = i == 0 ? theme::eq1Cyan
                                       : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            }
            spectrumView_->updateSpectrum(*lastAnalysis_);
        }
        eqCanvas_->setSnapshot(snap);

        // 2. Tarjetas de los tres ecuas
        for (int i = 0; i < 3; ++i) {
            DynamicEqCard::Snapshot card;
            card.title = juce::String::fromUTF8("Ecu dinámico ") + juce::String(i + 1);
            card.directionLabel = i == 0 ? juce::String::fromUTF8("Donde más se necesita")
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            card.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            card.enabled = dynamicEqEnabled_[i];
            card.maxGainDb = engine_.config().maxGainDb;
            {
                std::lock_guard<std::mutex> lock(analysisMutex_);
                if (lastAnalysis_ && lastAnalysis_->dynamicEqGainsL.size() > static_cast<size_t>(i))
                    card.gains = lastAnalysis_->dynamicEqGainsL[i];
            }
            {
                std::lock_guard<std::mutex> lock(sweepMutex_);
                if (sweepSteps_[i].bandIndex >= 0) {
                    card.activeBand = sweepSteps_[i].bandIndex;
                    card.channelBadge = activeChannel_[i];
                    card.statusText = juce::String(sweepSteps_[i].centerFreqHz, 0)
                        + juce::String::fromUTF8(" Hz · ")
                        + juce::String(sweepSteps_[i].gainDb, 1, true)
                        + juce::String::fromUTF8(" dB · suavizado ")
                        + juce::String(sweepSteps_[i].smoothingMs, 0)
                        + juce::String::fromUTF8(" ms");
                } else {
                    card.statusText = juce::String::fromUTF8("Esperando señal…");
                }
            }
            cards_[i]->setSnapshot(card);
        }
    }

    // === Presets (guardar/cargar/exportar) ===

    juce::File presetsDir() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("Presets");
    }

    juce::var serializeEngine() const {
        auto obj = new juce::DynamicObject();
        const auto c = engine_.config();
        obj->setProperty("maxGainDb", c.maxGainDb);
        obj->setProperty("smoothingFactor", c.smoothingFactor);
        obj->setProperty("noiseFloorDb", c.noiseFloorDb);
        obj->setProperty("noiseSubtractionEnabled", c.noiseSubtractionEnabled);
        obj->setProperty("correctionEnabled", c.correctionEnabled);
        obj->setProperty("targetSpl", c.targetSpl);
        obj->setProperty("audioDelayMs", c.audioDelayMs);
        // Curva del EQ manual (antes no se guardaba: el preset perdía lo
        // principal, M8)
        auto eqGains = new juce::DynamicObject();
        for (const auto& b : engine_.bands())
            eqGains->setProperty("band" + juce::String(b.index), b.gainDb);
        obj->setProperty("eqGains", eqGains);
        // Ecuas dinámicos: el estado real del motor (antes se serializaba un
        // estado muerto de la UI que nunca se modificaba ni se leía, M8/M9)
        auto eqs = new juce::DynamicObject();
        for (int i = 0; i < 3; ++i) {
            auto eq = new juce::DynamicObject();
            const auto cfg = engine_.dynamicEqConfig(i);
            eq->setProperty("enabled", engine_.dynamicEqEnabled(i));
            eq->setProperty("intervalMs", cfg.decisionIntervalMs);
            eq->setProperty("maxGainDb", cfg.maxGainDb);
            eq->setProperty("mixerLevel", cfg.mixerLevel);
            eq->setProperty("speedMultiplier", cfg.speedMultiplier);
            eq->setProperty("extraSweeps", cfg.extraSweeps);
            eqs->setProperty("eq" + juce::String(i + 1), eq);
        }
        obj->setProperty("dynamicEqs", eqs);
        return juce::var(obj);
    }

    void applyPhoneSync(const juce::var& payload, bool forceApply = false) {
        // Payload sincronizado desde la app móvil: perfiles, correcciones, ajustes
        auto* obj = payload.getDynamicObject();
        if (obj == nullptr) return;

        // Ajustes del móvil. La respuesta de cada sondeo lleva siempre su config
        // en vivo, pero el PC solo la APLICA en dos casos explícitos — nunca
        // automático, para que el sondeo de 3 s no vaya sobreescribiendo el PC:
        //  (a) staged: el móvil pulsó "Enviar mis ajustes al PC" (bandera
        //      sendToPc). El PC confirma con un push "acked" y el móvil borra
        //      su estado pendiente.
        //  (b) forceApply: el usuario pulsó "Recibir del móvil" en Ajustes.
        const auto cfg = obj->getProperty("config");
        auto* cfgObj = cfg.getDynamicObject();
        const bool staged = static_cast<bool>(obj->getProperty("sendToPc"));
        if (cfgObj != nullptr && (forceApply || staged)) {
            applyEngineConfigVar(juce::var(cfgObj));
            if (staged) {
                auto* ack = new juce::DynamicObject();
                ack->setProperty("acked", true);
                phoneLink_->pushSync(juce::var(ack), juce::String::fromUTF8("Ajustes del móvil aplicados"));
            }
        }

        // Comando del Hub enviado desde la app móvil (por USB)
        const auto hubCmd = obj->getProperty("hubCmd");
        auto* hc = hubCmd.getDynamicObject();
        if (hc != nullptr) {
            const int route = static_cast<int>(static_cast<double>(hc->getProperty("route")));
            const auto cmd = hc->getProperty("cmd").toString();
            const double value = static_cast<double>(hc->getProperty("value"));
            if (route >= 0 && route < RouteHub::NUM_ROUTES) {
                if (cmd == "mute") hub_->setRouteMute(route, value > 0.5);
                else if (cmd == "enable") hub_->setRouteEnabled(route, value > 0.5);
                else if (cmd == "invert") hub_->setPhaseInvert(route, value > 0.5);
                else if (cmd == "gain") hub_->setRouteGainDb(route, static_cast<float>(value));
                else if (cmd == "delay") hub_->setDelayMs(route, static_cast<float>(value));
            }
        }
    }

    void refreshPresets() {
        presetBox_.clear(juce::dontSendNotification);
        presetsDir().createDirectory();
        for (const auto& f : presetsDir().findChildFiles(juce::File::findFiles, false, "*.json"))
            presetBox_.addItem(f.getFileNameWithoutExtension(), presetBox_.getNumItems() + 1);
    }

    void saveCurrentPreset() {
        const auto file = presetsDir().getChildFile(
            "Preset " + juce::String(presetsDir().findChildFiles(juce::File::findFiles, false, "*.json").size() + 1) + ".json");
        file.replaceWithText(juce::JSON::toString(serializeEngine(), true));
        refreshPresets();
    }

    void loadSelectedPreset() {
        const auto f = presetsDir().getChildFile(presetBox_.getText() + ".json");
        if (!f.existsAsFile()) return;
        applyEngineConfigVar(juce::JSON::parse(f));
    }

    // Aplica un JSON de configuración (el mismo formato de serializeEngine())
    // al motor: campos escalares, curva del EQ manual y ecuas dinámicos.
    // Sirve tanto a los presets como a la sincronización con el móvil.
    void applyEngineConfigVar(const juce::var& v) {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        auto num = [](const juce::DynamicObject* o, const char* key, double def) {
            if (o == nullptr) return def;
            const auto val = o->getProperty(key);
            return val.isVoid() ? def : static_cast<double>(val);
        };
        auto flag = [](const juce::DynamicObject* o, const char* key, bool def) {
            if (o == nullptr) return def;
            const auto val = o->getProperty(key);
            return val.isVoid() ? def : static_cast<bool>(val);
        };
        auto c = engine_.config();
        c.maxGainDb = static_cast<float>(num(obj, "maxGainDb", c.maxGainDb));
        c.smoothingFactor = static_cast<float>(num(obj, "smoothingFactor", c.smoothingFactor));
        c.noiseFloorDb = static_cast<float>(num(obj, "noiseFloorDb", c.noiseFloorDb));
        c.noiseSubtractionEnabled = flag(obj, "noiseSubtractionEnabled", c.noiseSubtractionEnabled);
        c.correctionEnabled = flag(obj, "correctionEnabled", c.correctionEnabled);
        c.targetSpl = static_cast<float>(num(obj, "targetSpl", c.targetSpl));
        c.audioDelayMs = static_cast<float>(num(obj, "audioDelayMs", c.audioDelayMs));
        engine_.configure(c);

        // Curva del EQ manual
        if (auto* eqGains = obj->getProperty("eqGains").getDynamicObject())
            for (const auto& b : engine_.bands()) {
                const auto val = eqGains->getProperty("band" + juce::String(b.index));
                if (!val.isVoid())
                    engine_.setBandGain(b.index, static_cast<float>(static_cast<double>(val)));
            }

        // Ecuas dinámicos
        if (auto* eqs = obj->getProperty("dynamicEqs").getDynamicObject())
            for (int i = 0; i < 3; ++i) {
                if (auto* eq = eqs->getProperty("eq" + juce::String(i + 1)).getDynamicObject()) {
                    acoustical::DynamicEqConfig cfg;
                    cfg.decisionIntervalMs = static_cast<int>(num(eq, "intervalMs", cfg.decisionIntervalMs));
                    cfg.maxGainDb = static_cast<float>(num(eq, "maxGainDb", cfg.maxGainDb));
                    cfg.mixerLevel = static_cast<float>(num(eq, "mixerLevel", cfg.mixerLevel));
                    cfg.speedMultiplier = static_cast<float>(num(eq, "speedMultiplier", cfg.speedMultiplier));
                    cfg.extraSweeps = static_cast<int>(num(eq, "extraSweeps", cfg.extraSweeps));
                    cfg.clamp();
                    engine_.setDynamicEqConfig(i, cfg);
                    const bool enabled = flag(eq, "enabled", engine_.dynamicEqEnabled(i));
                    engine_.setDynamicEqEnabled(i, enabled);
                    dynamicEqEnabled_[i] = enabled;
                }
            }
        settingsPanel_->refresh();
    }

    // === Layout ===

    void resized() override {
        auto bounds = getLocalBounds();
        if (eqCardsPanel_.isShowing()) {
            auto panel = eqCardsPanel_.getLocalBounds();
            const int cardH = panel.getHeight() / 3;
            for (int i = 0; i < 3; ++i)
                cards_[i]->setBounds(panel.removeFromTop(cardH).reduced(10, 5));
        }
        auto top = bounds.removeFromTop(44.0f).reduced(8.0f, 6.0f);
        // Ancho fijo total ~1014 px: con la ventana mínima (1280) quedan
        // ~250 px para el estado del móvil; antes eran ~30 px y se recortaba.
        powerButton_.setBounds(top.removeFromLeft(72.0f));
        referenceButton_.setBounds(top.removeFromLeft(92.0f).reduced(2.0f, 0.0f));
        noiseButton_.setBounds(top.removeFromLeft(66.0f).reduced(2.0f, 0.0f));
        freezeButton_.setBounds(top.removeFromLeft(84.0f).reduced(2.0f, 0.0f));
        viewBox_.setBounds(top.removeFromLeft(130.0f).reduced(4.0f, 0.0f));
        presetBox_.setBounds(top.removeFromLeft(120.0f).reduced(4.0f, 0.0f));
        savePresetButton_.setBounds(top.removeFromLeft(76.0f).reduced(2.0f, 0.0f));
        syncButton_.setBounds(top.removeFromLeft(92.0f).reduced(2.0f, 0.0f));
        fullscreenButton_.setBounds(top.removeFromLeft(106.0f).reduced(2.0f, 0.0f));
        lockButton_.setBounds(top.removeFromLeft(84.0f).reduced(2.0f, 0.0f));
        undoButton_.setBounds(top.removeFromLeft(46.0f).reduced(2.0f, 0.0f));
        redoButton_.setBounds(top.removeFromLeft(46.0f).reduced(2.0f, 0.0f));
        phoneLabel_.setBounds(top);

        tabs_->setBounds(bounds);
    }

    juce::AudioDeviceManager& deviceManager_;
    acoustical::AcousticalEngine& engine_;
    AsioBridgeClient bridge_;   // lado de app del puente ASIO (debe vivir más que audioTab_)

    juce::TextButton powerButton_, referenceButton_, noiseButton_, freezeButton_,
        savePresetButton_, syncButton_, fullscreenButton_, lockButton_,
        undoButton_, redoButton_;
    juce::ComboBox presetBox_, viewBox_;
    juce::Label phoneLabel_;

    std::deque<acoustical::AudioConfig> undoStack_, redoStack_;
    bool fadersLocked_ = false;

    std::unique_ptr<juce::TabbedComponent> tabs_;
    std::unique_ptr<EqCanvas> eqCanvas_;
    std::array<std::unique_ptr<DynamicEqCard>, 3> cards_;
    juce::Component eqCardsPanel_;
    std::unique_ptr<SpectrumView> spectrumView_;
    std::unique_ptr<RoutingMatrix> routingMatrix_;
    std::unique_ptr<SettingsPanel> settingsPanel_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioPanel_;
    std::unique_ptr<AudioTab> audioTab_;
    std::unique_ptr<RouteHub> hub_;          // antes que hubPanel_ (orden de destrucción)
    std::unique_ptr<HubPanel> hubPanel_;

    std::unique_ptr<PhoneLink> phoneLink_;

    // Estado de los ecuas dinámicos para la UI
    std::array<bool, 3> dynamicEqEnabled_{{true, false, false}};
    std::array<acoustical::SweepStep, 3> sweepSteps_{};
    std::array<int, 3> activeBand_{{-1, -1, -1}};
    std::array<juce::String, 3> activeChannel_{{"L+R", "L+R", "L+R"}};

    std::mutex analysisMutex_;
    std::unique_ptr<acoustical::AnalysisResult> lastAnalysis_;
    std::mutex sweepMutex_;
    std::mutex dspMutex_;

    double lastSampleRate_ = 48000.0;
    std::vector<float> genBuf_;          // señal del generador (una generación por bloque)
    std::vector<float> bridgeBufL_, bridgeBufR_;  // audio entrante de Cubase (puente ASIO)
};
