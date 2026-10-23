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
// v1.10.0: el hub del PC (RouteHub.h / HubPanel.h) se retiró; la pestaña
// "Ruteos" (RoutingMatrix) sigue siendo la de entradas/salidas.
#include "PhoneLink.h"
#include "PhoneSyncPayload.h"
#include "RemoteAudioLink.h"
#include "AsioBridgeClient.h"
#include "StartupLog.h"

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
        startuplog::log(juce::String::fromUTF8("consola: construyendo la interfaz…"));
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
        // procesa la respuesta (la config del móvil en vivo) en el mismo
        // intercambio. (v1.10.0: ya no hay comandos de Hub en la respuesta;
        // el hub se retiró.)
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
        // v1.10.0: la pestaña "Hub" se retiró; renumerado 1-6.
        addAndMakeVisible(viewBox_);
        viewBox_.addItem(juce::String::fromUTF8("1 · EQ"), 1);
        viewBox_.addItem(juce::String::fromUTF8("2 · Ecuas dinámicos"), 2);
        viewBox_.addItem(juce::String::fromUTF8("3 · Ruteos"), 3);
        viewBox_.addItem(juce::String::fromUTF8("4 · Ajustes"), 4);
        viewBox_.addItem(juce::String::fromUTF8("5 · Análisis"), 5);
        viewBox_.addItem(juce::String::fromUTF8("6 · Audio"), 6);
        viewBox_.setSelectedId(1, juce::dontSendNotification);
        viewBox_.onChange = [this] {
            tabs_->setCurrentTabIndex(viewBox_.getSelectedId() - 1, true);
        };

        addAndMakeVisible(fullscreenButton_);
        fullscreenButton_.setButtonText("Pantalla (F11)");
        fullscreenButton_.setClickingTogglesState(true);
        fullscreenButton_.setTooltip(juce::String::fromUTF8("Pantalla completa (F11)"));
        fullscreenButton_.onClick = [this] { toggleFullscreen(); };
        startuplog::log(juce::String::fromUTF8("consola: barra superior lista"));

        // === Panel de audio: todas las tarjetas del PC con sus ajustes ===
        audioPanel_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager_, 0, 16, 0, 16, false, false, true, false);
        audioPanel_->setItemHeight(36);
        audioTab_ = std::make_unique<AudioTab>(*audioPanel_, bridge_);
        startuplog::log(juce::String::fromUTF8("consola: panel de audio listo"));

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
        startuplog::log(juce::String::fromUTF8("consola: EQ y ecuas dinámicos listos"));

        // === Pestañas ===
        // Enlace con el móvil (Wi-Fi con respaldo USB): se crea antes que el
        // panel de Ajustes porque este muestra su estado y el emparejamiento
        auto adb = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getParentDirectory().getChildFile("adb/adb.exe");
        phoneLink_ = std::make_unique<PhoneLink>(adb);
        phoneLink_->onSync = [this](const juce::var& payload) { applyPhoneSync(payload); };
        startuplog::log(juce::String::fromUTF8("consola: enlace del móvil listo"));
        // Puente de audio Wi-Fi (M2/M3): descubre los móviles por baliza; su
        // micrófono pasa a ser una entrada del PC (fila en la pestaña Ruteos).
        // (v1.10.0: la otra dirección del puente, PC → altavoces del móvil,
        // la llamaba la ruta de red del Hub, que se retiró.)
        remoteAudio_ = std::make_unique<RemoteAudioLink>();
        remoteAudio_->setCodeProvider([pl = phoneLink_.get()] { return pl->pairCode(); });
        phoneLink_->onBeacon = [ra = remoteAudio_.get()](const juce::String& ip, const juce::var& info) {
            ra->noteBeacon(ip, info.getProperty("id", juce::var()).toString(),
                           info.getProperty("dev", juce::var()).toString());
        };
        remoteAudio_->start();
        phoneLink_->startWatchdog();
        startuplog::log(juce::String::fromUTF8("consola: oyente UDP y baliza activos"));

        spectrumView_ = std::make_unique<SpectrumView>();
        routingMatrix_ = std::make_unique<RoutingMatrix>(deviceManager_, *remoteAudio_);
        loadSavedSettings();  // últimos valores funcionales
        settingsPanel_ = std::make_unique<SettingsPanel>(engine_, phoneLink_.get(),
            [this](bool active) {
                // El combo "Señal" de Ajustes es el ÚNICO control del
                // generador: cada forma de onda distinta de Silencio lo
                // enciende (y suena al instante por la salida y el puente
                // ASIO); "Silencio" lo apaga y la entrada pasa tal cual.
                // (El antiguo toggle "Generador activo" se eliminó: era
                // redundante con el menú y permitía estados en los que el
                // combo decía una cosa y el audio hacía otra.)
                generatorActive_.store(active);
            },
            spatialX_, spatialY_, spatialZ_, spatialSize_,
            [this](const acoustical_mode_profile* m) { applyModeProfile(m); },
            [this]() -> std::string { return activeModeId_; },
            [this] {
                // Modo rápido (no del catálogo): el modo activo deja de ser
                // un perfil y la persistencia se actualiza (id vacío).
                activeModeId_.clear();
                saveSettings();
            });
        loadActiveMode();  // modo activo persistente (v1.8): catálogo + ajustes guardados
        startuplog::log(juce::String::fromUTF8("consola: ruteos y ajustes guardados listos"));
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
        // v1.10.0: sin hub del PC (tarjeta y pestaña retiradas).
        tabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs_->addTab("EQ", theme::surface, eqCanvas_.get(), false);
        tabs_->addTab(juce::String::fromUTF8("Ecuas dinámicos"), theme::surface, &eqCardsPanel_, false);
        tabs_->addTab("Ruteos", theme::surface, routingMatrix_.get(), false);
        tabs_->addTab("Ajustes", theme::surface, settingsPanel_.get(), false);
        tabs_->addTab(juce::String::fromUTF8("Análisis"), theme::surface, spectrumView_.get(), false);
        tabs_->addTab("Audio", theme::surface, audioTab_.get(), false);
        addAndMakeVisible(*tabs_);
        startuplog::log(juce::String::fromUTF8("consola: pestañas listas"));

        refreshPresets();
        engine_.onAnalysis = [this](const acoustical::AnalysisResult& r) { storeAnalysis(r); };
        // v1.11.0 (lote 3): sonda del auto-chequeo (el motor la dispara a la
        // cadencia de WorkConfig.autoCheck; la consola guarda la última para
        // que la pestaña Ajustes la muestre).
        engine_.onProbe = [this](const acoustical::ProbeResult& p) { storeProbe(p); };
        engine_.onSweep = [this](acoustical::SweepProcess p, const acoustical::SweepStep& s) {
            storeSweep(p, s);
        };
        startuplog::log(juce::String::fromUTF8("consola: presets y callbacks listos"));

        startTimerHz(30);
        deviceManager_.addAudioCallback(this);
        startuplog::log(juce::String::fromUTF8("consola: callback de audio registrado"));

        // Motor arrancado por defecto: solo hay que tener el audio seleccionado
        if (engine_.start()) powerButton_.setButtonText("Parar");
        startuplog::log(juce::String::fromUTF8("consola: lista (")
            + (engine_.isRunning() ? juce::String::fromUTF8("motor arrancado")
                                   : juce::String::fromUTF8("motor sin arrancar"))
            + juce::String::fromUTF8(")"));
    }

    // Control del generador de señales (desde Ajustes): el combo "Señal"
    // es el único interruptor — "Silencio" apaga y el resto enciende al instante.
    std::atomic<bool> generatorActive_{false};

    // === Posición espacial (v1.8) ===
    // La UI escribe (Ajustes > Espacio, sync del móvil, modo activo) y el
    // callback de audio lee: los MISMOS atómicos, sin copias que se queden
    // viejas. Ley de compensación (ganancia + retardo) en el core C: la misma
    // que el móvil y el plugin. z=0.5/size=0.5 es la posición neutra (0 dB/0 ms).
    std::atomic<float> spatialX_{0.0f}, spatialY_{0.0f},
        spatialZ_{0.5f}, spatialSize_{0.5f};
    // Anillos de retardo de la salida (se asignan una vez si el retardo pide
    // más; cero malloc en el camino de audio salvo ese cambio puntual).
    std::vector<float> spatialRingL_, spatialRingR_;
    int spatialWritePos_ = 0;
    bool spatialWasActive_ = false;   // solo hilo de audio

    // Modo de trabajo activo (v1.8): "" = ajustes personalizados; si hay id
    // válida se persiste en settings.json y se recarga en el arranque.
    std::string activeModeId_;

    ~ConsoleComponent() override {
        phoneLink_->stopWatchdog();
        deviceManager_.removeAudioCallback(this);
        remoteAudio_->stop();
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
        // Teclas 1-6: saltar directo a cada vista
        const auto ch = key.getTextCharacter();
        if (ch >= '1' && ch <= '6') {
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
        // Modo de trabajo activo (v1.8): solo se GUARDA aquí; se APLICA en
        // loadActiveMode() (el constructor, ya con el panel creado).
        const auto modeVal = obj->getProperty("activeModeId");
        if (!modeVal.isVoid() && !modeVal.toString().isEmpty())
            activeModeId_ = modeVal.toString().toStdString();
        // Posición espacial (v1.10.1: antes no se guardaba y cada arranque
        // volvía al neutro aunque el usuario la hubiera movido).
        storeSpatial(phonesync::spatialOf(obj));
    }

    // Publica una posición espacial leída de un JSON (settings, preset o
    // móvil) en los atómicos que lee el callback de audio.
    void storeSpatial(const phonesync::Spatial& sp) {
        if (!sp.present) return;
        spatialX_.store(sp.x, std::memory_order_relaxed);
        spatialY_.store(sp.y, std::memory_order_relaxed);
        spatialZ_.store(sp.z, std::memory_order_relaxed);
        spatialSize_.store(sp.size, std::memory_order_relaxed);
    }

    // === Modos de trabajo (v1.8) ===
    // Aplica el perfil del catálogo (core C: motor + 3 ecuas con sus bandas de
    // apoyo + posición espacial + generador) al motor en vivo, persiste el id
    // del modo y redibuja el panel. Desde la UI (botones de Ajustes), del
    // móvil (sync) y del arranque (loadActiveMode). Todo lo aplicado sigue
    // editable: cada control guarda por su cuenta al cambiar.
    void applyModeProfile(const acoustical_mode_profile* m) {
        if (!m) return;
        pushUndo();  // un modo es deshacible como cualquier otro ajuste
        auto c = engine_.config();
        c.targetSpl = m->targetSpl;
        c.maxGainDb = m->maxGainDb;
        c.bandCount = static_cast<acoustical::BandCount>(m->bandCountIndex);
        c.fftSize = static_cast<acoustical::FftSize>(m->fftSizeIndex);
        c.analysisInterval = static_cast<acoustical::AnalysisInterval>(m->analysisIntervalIndex);
        c.smoothingFactor = m->smoothingFactor;
        c.correctionEnabled = m->correctionEnabled != 0;
        c.noiseSubtractionEnabled = m->noiseSubtractionEnabled != 0;
        c.audioDelayMs = m->audioDelayMs;
        engine_.configure(c);

        for (int i = 0; i < 3; ++i) {
            const auto& e = m->eqs[i];
            acoustical::DynamicEqConfig cfg;
            cfg.startFrom = e.startFrom == 1 ? acoustical::SweepDirection::BOTTOM_UP
                          : e.startFrom == 2 ? acoustical::SweepDirection::TOP_DOWN
                                              : acoustical::SweepDirection::NEED_BASED;
            cfg.decisionIntervalMs = e.intervalMs;
            cfg.maxGainDb = e.maxGainDb;
            cfg.mixerLevel = e.mixerLevel;
            cfg.speedMultiplier = e.speedMultiplier;
            cfg.extraSweeps = e.extraSweeps;
            cfg.clamp();
            engine_.setDynamicEqConfig(i, cfg);
            engine_.setDynamicEqEnabled(i, e.enabled != 0);
            dynamicEqEnabled_[i] = e.enabled != 0;
            std::vector<acoustical::SupportBand> bands;
            for (int k = 0; k < 4; ++k)
                bands.push_back({e.support[k].frequencyHz, e.support[k].gainDb, e.support[k].q});
            engine_.setSupportBands(i, bands);
        }

        // Espacio: atómicos (el callback de audio los lee por bloque)
        spatialX_.store(m->spatialX, std::memory_order_relaxed);
        spatialY_.store(m->spatialY, std::memory_order_relaxed);
        spatialZ_.store(m->spatialZ, std::memory_order_relaxed);
        spatialSize_.store(m->spatialSize, std::memory_order_relaxed);

        // Generador de referencia (en el catálogo todos arrancan en Silencio)
        using W = acoustical::SignalGenerator::Waveform;
        const auto w = m->generatorWaveform == ACOUSTICAL_WAVE_SINE ? W::Sine
                       : m->generatorWaveform == ACOUSTICAL_WAVE_BAND_SINE ? W::BandSine
                       : m->generatorWaveform == ACOUSTICAL_WAVE_LOG_SWEEP ? W::LogSweep
                       : m->generatorWaveform == ACOUSTICAL_WAVE_PINK ? W::PinkNoise
                       : m->generatorWaveform == ACOUSTICAL_WAVE_WHITE ? W::WhiteNoise
                       : W::Silence;
        engine_.signalGenerator().setWaveform(w);
        engine_.signalGenerator().setLevelDb(m->generatorLevelDb);
        generatorActive_.store(w != W::Silence);

        activeModeId_ = m->id;
        saveSettings();
        if (settingsPanel_) settingsPanel_->refresh();
    }

    // Arranque: si hay modo activo guardado, se aplica su perfil del catálogo
    // y DESPUÉS los ajustes que el usuario guardó encima de ese modo (su
    // último settings.json), para que los cambios hechos en el modo sobrevivan
    // al reinicio: el catálogo da la base, el JSON la personalización.
    void loadActiveMode() {
        if (activeModeId_.empty()) return;
        const auto* m = acoustical_mode_by_id(activeModeId_.c_str());
        if (!m) {
            activeModeId_.clear();
            return;
        }
        // Los ajustes guardados se leen ANTES de aplicar el modo:
        // applyModeProfile() termina con saveSettings(), que reescribe
        // settings.json con el catálogo puro — leerlo después (como hacía la
        // v1.8-v1.10.0) devolvía el catálogo y los cambios del usuario dentro
        // del modo se perdían en cada arranque.
        juce::var saved;
        if (const auto f = settingsFile(); f.existsAsFile())
            saved = juce::JSON::parse(f);
        applyModeProfile(m);
        if (saved.getDynamicObject() != nullptr) {
            applyEngineConfigVar(saved);
            saveSettings();   // el fichero vuelve a ser catálogo + ajustes
        }
        // El arranque no es un cambio del usuario: nada que deshacer.
        undoStack_.clear();
        startuplog::log(juce::String::fromUTF8("consola: modo activo ")
            + juce::String::fromUTF8(m->name) + juce::String::fromUTF8(" cargado"));
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int numInputs,
                                          float* const* output, int numOutputs,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        // Contador para el log de diagnóstico (una sola vez; aquí NO se
        // escribe en disco: esto corre en el hilo de audio).
        audioBursts_.fetch_add(1, std::memory_order_relaxed);
        // Buffer preasignado: se redimensiona solo si crece y se pone a cero
        // (aquí se va sumando) cada bloque. Evita un malloc en cada llamada.
        if (monoBuf_.size() < static_cast<size_t>(numSamples))
            monoBuf_.resize(static_cast<size_t>(numSamples));
        std::fill_n(monoBuf_.data(), numSamples, 0.0f);
        auto& mono = monoBuf_;
        // Niveles de entrada (Ruteos > Entradas): ganancia por canal,
        // atómica (escrita por la UI, leída aquí) y aplicada antes del motor.
        const float inL = routingMatrix_->inputGainL();
        const float inR = routingMatrix_->inputGainR();
        for (int ch = 0; ch < numInputs; ++ch)
            if (const auto* in = input[ch]) {
                const float g = ch == 0 ? inL : (ch == 1 ? inR : 1.0f);
                for (int s = 0; s < numSamples; ++s) mono[s] += in[s] * g;
            }

        // Móviles (M2): cada micrófono remoto es una entrada más, con su
        // ganancia propia (Ruteos > Entradas > Móviles). Llegan por UDP a
        // 48 kHz; pullMic resamplea a la tasa del dispositivo.
        {
            const auto mics = remoteAudio_->activeMicIps();
            for (const auto& ip : mics) {
                if (remoteMicBuf_.size() < static_cast<size_t>(numSamples))
                    remoteMicBuf_.resize(static_cast<size_t>(numSamples));
                remoteAudio_->pullMic(ip, remoteMicBuf_.data(), numSamples,
                                      lastSampleRate_);
                const float g = remoteAudio_->micGain(ip);
                for (int s = 0; s < numSamples; ++s)
                    mono[s] += remoteMicBuf_[static_cast<size_t>(s)] * g;
            }
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
        // Buffers preasignados (se redimensionan solo si crecen).
        if (leftBuf_.size() < static_cast<size_t>(numSamples))
            leftBuf_.resize(static_cast<size_t>(numSamples));
        if (rightBuf_.size() < static_cast<size_t>(numSamples))
            rightBuf_.resize(static_cast<size_t>(numSamples));
        auto& left = leftBuf_;
        auto& right = rightBuf_;
        {
            std::lock_guard<std::mutex> lock(dspMutex_);
            std::copy(mono.begin(), mono.end(), left.begin());
            std::copy(mono.begin(), mono.end(), right.begin());
            engine_.eqDspL().process(left.data(), numSamples);
            engine_.eqDspR().process(right.data(), numSamples);
        }

        // v1.10.0: sin hub (la salida post-EQ va directa a la tarjeta; la
        // ganancia global de la pestaña Ruteos sigue aplicándose debajo).

        // Volumen general (pestaña Ruteos): escala solo la salida a la tarjeta
        const float outGain = routingMatrix_->gain();
        if (outGain != 1.0f)
            for (int s = 0; s < numSamples; ++s) {
                left[s] *= outGain;
                right[s] *= outGain;
            }

        // === Compensación espacial (v1.8) ===
        // La MISMA ley del móvil y del plugin (core C): según la posición
        // (x lateral, y frente/detrás, z distancia 0–15 m, size del elemento)
        // se aplica ganancia (20·log10 distancia + tamaño + off-axis) y el
        // retardo de propagación d/343·1000 al final de la cadena (post-EQ +
        // nivel): "lejos y grande" llega al oído con el mismo nivel y
        // timing que "cerca y pequeño". Solo si hay compensación (el neutro
        // z=0.5/size=0.5/x=0 da 0 dB/0 ms y no toca la señal: desde la
        // v1.10.1 la ley del core es relativa a ese neutro).
        {
            const float sx = spatialX_.load(std::memory_order_relaxed);
            const float sy = spatialY_.load(std::memory_order_relaxed);
            const float sz = spatialZ_.load(std::memory_order_relaxed);
            const float ss = spatialSize_.load(std::memory_order_relaxed);
            float gainDb = 0.0f, delayMs = 0.0f;
            acoustical_spatial_compensate(sx, sy, sz, ss, &gainDb, &delayMs);
            const bool active = gainDb != 0.0f || delayMs > 0.0f;
            if (active) {
                const int need = static_cast<int>(delayMs * lastSampleRate_ / 1000.0f);
                const int minRing = need + numSamples + 2;
                if (static_cast<int>(spatialRingL_.size()) < minRing) {
                    // Solo si el bloque supera lo preasignado en
                    // audioDeviceAboutToStart (allí se reserva el peor caso).
                    spatialRingL_.assign(static_cast<size_t>(minRing + 512), 0.0f);
                    spatialRingR_.assign(spatialRingL_.size(), 0.0f);
                    spatialWritePos_ = 0;
                } else if (!spatialWasActive_) {
                    // Volver a compensar tras un rato en el neutro: el anillo
                    // guarda el audio de la última vez (puede ser de hace
                    // minutos) y la línea lo soltaría por la salida. Se vacía.
                    std::fill(spatialRingL_.begin(), spatialRingL_.end(), 0.0f);
                    std::fill(spatialRingR_.begin(), spatialRingR_.end(), 0.0f);
                    spatialWritePos_ = 0;
                }
                const int ringSize = static_cast<int>(spatialRingL_.size());
                const int delay = juce::jlimit(0, ringSize - numSamples - 1, need);
                const float g = (gainDb != 0.0f) ? std::pow(10.0f, gainDb / 20.0f) : 1.0f;
                for (int ch = 0; ch < 2; ++ch) {
                    auto& ring = (ch == 0 ? spatialRingL_ : spatialRingR_);
                    auto& out = (ch == 0 ? left : right);
                    for (int s = 0; s < numSamples; ++s) {
                        const int w = (spatialWritePos_ + s) % ringSize;
                        const int r = (w - delay + ringSize) % ringSize;
                        ring[w] = out[s] * g;      // la ganancia entra en la línea…
                        out[s] = ring[r];         // …y sale la muestra retardada ya ganada
                    }
                }
                spatialWritePos_ = (spatialWritePos_ + numSamples) % ringSize;
            }
            spatialWasActive_ = active;
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
        // Línea de la compensación espacial preasignada para el peor caso
        // (z = 1: 7.5 m más que el neutro) + el bloque: el callback de audio
        // no tiene que hacer malloc al mover el control de distancia.
        const int block = device ? device->getCurrentBufferSizeSamples() : 2048;
        const double maxMs =
            (ACOUSTICAL_SPATIAL_MAX_METERS - ACOUSTICAL_SPATIAL_NEUTRAL_METERS) / 343.0 * 1000.0;
        const auto size = static_cast<size_t>(std::ceil(maxMs * lastSampleRate_ / 1000.0))
                          + static_cast<size_t>(juce::jmax(block, 1)) + 2 + 512;
        spatialRingL_.assign(size, 0.0f);
        spatialRingR_.assign(size, 0.0f);
        spatialWritePos_ = 0;
        spatialWasActive_ = false;
    }
    void audioDeviceStopped() override {}
    // El dispositivo avisó de un error (tarjeta desconectada, driver que
    // falla…). Corre en el hilo de audio: solo guardamos el mensaje y lo
    // volca al log el timer de UI (no se escribe en disco desde aquí).
    void audioDeviceError(const juce::String& errorMessage) override {
        const std::lock_guard<std::mutex> lock(analysisMutex_);
        if (audioError_ != errorMessage) audioError_ = errorMessage;
    }

    // === Estado compartido entre el hilo del motor y la UI ===

    void storeAnalysis(const acoustical::AnalysisResult& r) {
        std::lock_guard<std::mutex> lock(analysisMutex_);
        lastAnalysis_ = std::make_unique<acoustical::AnalysisResult>(r);
    }

    // v1.11.0 (lote 3): última sonda del auto-chequeo (mismo lock que el
    // análisis: el timer de la consola copia ambos a 30 Hz para el panel).
    void storeProbe(const acoustical::ProbeResult& p) {
        std::lock_guard<std::mutex> lock(analysisMutex_);
        lastProbe_ = std::make_unique<acoustical::ProbeResult>(p);
    }

    void storeSweep(acoustical::SweepProcess p, const acoustical::SweepStep& s) {
        const int i = static_cast<int>(p);
        std::lock_guard<std::mutex> lock(sweepMutex_);
        sweepSteps_[i] = s;
        activeBand_[i] = s.bandIndex;
        activeChannel_[i] = s.channel;
    }

    void timerCallback() override {
        // 0. Espacio (v1.8): la lectura de compensación del panel se recalcula
        //     de los atómicos (misma ley que la cadena de salida de audio).
        if (settingsPanel_) settingsPanel_->tickSpatialReadout();
        // 0a'. v1.11.0 (lote 3): la pestaña Ajustes se refresca a 30 Hz:
        //     gauge de SPL (nivel del último análisis), última sonda del
        //     auto-chequeo y espectro de la referencia capturada. Los valores
        //     se copian bajo el lock del análisis (el motor despacha desde su
        //     hilo) y el panel solo pinta.
        {
            float splNow = 0.0f;
            const acoustical::ProbeResult* probeNow = nullptr;
            {
                const std::lock_guard<std::mutex> lock(analysisMutex_);
                if (lastAnalysis_) splNow = lastAnalysis_->spl;
                if (lastProbe_) probeNow = &*lastProbe_;
            }
            if (settingsPanel_) settingsPanel_->tickLive(splNow, probeNow);
        }
        // 0a. Diagnóstico: primer bloque de audio recibido (una sola vez) y
        //     cualquier error del dispositivo pendiente de volcar al log.
        if (!audioLogged_ && audioBursts_.load(std::memory_order_relaxed) > 0) {
            audioLogged_ = true;
            startuplog::log(juce::String::fromUTF8(
                "audio: la tarjeta está entregando bloques de audio (el bucle "
                "funciona)"));
        }
        {
            const std::lock_guard<std::mutex> lock(analysisMutex_);
            if (!audioError_.isEmpty()) {
                startuplog::log(juce::String::fromUTF8("audio: ERROR del dispositivo: ")
                    + audioError_);
                audioError_ = juce::String();
            }
        }
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
        // Modo de trabajo activo (v1.8): el id del perfil del catálogo; al
        // arrancar la app se vuelve a aplicar (con los ajustes guardados).
        obj->setProperty("activeModeId", juce::String::fromUTF8(activeModeId_.c_str()));
        // Mismo dato con el nombre que lee el móvil (applyRemoteConfig de
        // Android lee "modeId"; con solo "activeModeId" el modo del PC nunca
        // le llegaba).
        obj->setProperty("modeId", juce::String::fromUTF8(activeModeId_.c_str()));
        // Posición espacial: se persiste (settings/presets) y viaja al móvil
        // con "Sincronizar" — el móvil la lee de aquí ("spatial").
        {
            auto* sp = new juce::DynamicObject();
            sp->setProperty("x", spatialX_.load(std::memory_order_relaxed));
            sp->setProperty("y", spatialY_.load(std::memory_order_relaxed));
            sp->setProperty("z", spatialZ_.load(std::memory_order_relaxed));
            sp->setProperty("size", spatialSize_.load(std::memory_order_relaxed));
            obj->setProperty("spatial", juce::var(sp));
        }
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
            // Bandas de apoyo (v1.8): se persisten para que los cambios del
            // usuario dentro del modo activo (y el banco del catálogo)
            // sobrevivan al reinicio.
            auto sup = new juce::DynamicObject();
            const auto bands = engine_.supportBands(i);
            for (int k = 0; k < static_cast<int>(bands.size()) && k < 4; ++k) {
                auto b = new juce::DynamicObject();
                b->setProperty("freq", bands[k].frequencyHz);
                b->setProperty("gain", bands[k].gainDb);
                b->setProperty("q", bands[k].q);
                sup->setProperty("b" + juce::String(k + 1), b);
            }
            eq->setProperty("support", sup);
            eqs->setProperty("eq" + juce::String(i + 1), eq);
        }
        obj->setProperty("dynamicEqs", eqs);
        return juce::var(obj);
    }

    void applyPhoneSync(const juce::var& reply, bool forceApply = false) {
        // Respuesta de sync del móvil: un SOBRE {"type","ok","payload":{…}}
        // con la config en vivo en payload.config (ver PhoneSyncPayload.h).
        // Hasta la v1.10.0 se leía el nivel de arriba del sobre, donde no hay
        // nada: "Recibir del móvil" no aplicaba ningún ajuste y el modo y el
        // espacio del móvil nunca llegaban.
        //
        // La config viaja en CADA respuesta, pero el PC solo la APLICA en dos
        // casos explícitos — nunca en el sondeo automático de 3 s, que si no
        // reescribiría el PC (y reharía el panel de Ajustes) cada 3 s:
        //  (a) staged: el móvil pidió enviar sus ajustes al PC (sendToPc). El
        //      PC confirma con un push "acked" y el móvil borra lo pendiente.
        //  (b) forceApply: el usuario pulsó "Recibir del móvil" en Ajustes.
        //
        // Orden, la misma regla que el arranque: el modo del catálogo es la
        // base (solo si cambia) y la config del móvil va ENCIMA (sus ajustes
        // dentro del modo, su posición espacial).
        auto* cfgObj = phonesync::configOf(reply);
        const bool staged = phonesync::stagedOf(reply);
        if (cfgObj == nullptr || !(forceApply || staged)) return;

        const auto modeId = phonesync::modeIdOf(cfgObj);
        if (modeId.isNotEmpty() && modeId.toStdString() != activeModeId_)
            if (const auto* m = acoustical_mode_by_id(modeId.toRawUTF8()))
                applyModeProfile(m);   // deshacible; guarda y redibuja

        applyEngineConfigVar(juce::var(cfgObj));   // incluye la posición espacial
        saveSettings();

        if (staged) {
            auto* ack = new juce::DynamicObject();
            ack->setProperty("acked", true);
            phoneLink_->pushSync(juce::var(ack), juce::String::fromUTF8("Ajustes del móvil aplicados"));
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
                    // Bandas de apoyo persistidas (v1.8)
                    if (auto* sup = eq->getProperty("support").getDynamicObject()) {
                        std::vector<acoustical::SupportBand> bands;
                        for (int k = 1; k <= 4; ++k)
                            if (auto* b = sup->getProperty("b" + juce::String(k)).getDynamicObject())
                                bands.push_back({
                                    static_cast<float>(b->getProperty("freq").isVoid() ? 1000.0 : static_cast<double>(b->getProperty("freq"))),
                                    static_cast<float>(b->getProperty("gain").isVoid() ? 0.0 : static_cast<double>(b->getProperty("gain"))),
                                    static_cast<float>(b->getProperty("q").isVoid() ? 2.0 : static_cast<double>(b->getProperty("q")))});
                        if (!bands.empty())
                            engine_.setSupportBands(i, bands);
                    }
                }
            }
        storeSpatial(phonesync::spatialOf(obj));
        if (settingsPanel_) settingsPanel_->refresh();
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
    // Puente de audio Wi-Fi (M2/M3). Declarado ANTES que routingMatrix_
    // (la usa por referencia) y que phoneLink_ (su proveedor de código la
    // apunta): se destruye después de todos.
    std::unique_ptr<RemoteAudioLink> remoteAudio_;
    std::unique_ptr<RoutingMatrix> routingMatrix_;
    std::unique_ptr<SettingsPanel> settingsPanel_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioPanel_;
    std::unique_ptr<AudioTab> audioTab_;

    std::unique_ptr<PhoneLink> phoneLink_;

    // Estado de los ecuas dinámicos para la UI
    std::array<bool, 3> dynamicEqEnabled_{{true, false, false}};
    std::array<acoustical::SweepStep, 3> sweepSteps_{};
    std::array<int, 3> activeBand_{{-1, -1, -1}};
    std::array<juce::String, 3> activeChannel_{{"L+R", "L+R", "L+R"}};

    std::mutex analysisMutex_;
    std::unique_ptr<acoustical::AnalysisResult> lastAnalysis_;
    // v1.11.0 (lote 3): última sonda del auto-chequeo (protegida por analysisMutex_).
    std::unique_ptr<acoustical::ProbeResult> lastProbe_;
    std::mutex sweepMutex_;
    std::mutex dspMutex_;

    double lastSampleRate_ = 48000.0;
    // Diagnóstico (ver StartupLog.h): el callback suma, el timer volca al log
    std::atomic<int> audioBursts_{0};
    bool audioLogged_ = false;
    juce::String audioError_;   // protegido por analysisMutex_
    std::vector<float> genBuf_;          // señal del generador (una generación por bloque)
    std::vector<float> bridgeBufL_, bridgeBufR_;  // audio entrante de Cubase (puente ASIO)
    std::vector<float> remoteMicBuf_;    // micrófono remoto en curso (M2, una entrada por móvil)
    // Buffers preasignados del callback de audio: se redimensionan solo si
    // crecen, para no hacer malloc en el hilo de audio cada bloque (causaba
    // jitter; el mismo problema que ya se resolvió con genBuf_/bridgeBuf_).
    std::vector<float> monoBuf_;           // mezcla de entradas (tarjeta+móviles+gen+puente)
    std::vector<float> leftBuf_, rightBuf_; // salida post-EQ por canal
};
