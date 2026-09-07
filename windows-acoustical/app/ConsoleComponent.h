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
#include "PhoneLink.h"

class ConsoleComponent : public juce::Component,
                         private juce::AudioIODeviceCallback,
                         private juce::Timer {
public:
    ConsoleComponent(juce::AudioDeviceManager& dm, acoustical::AcousticalEngine& eng)
        : deviceManager_(dm), engine_(eng) {
        theme::apply(*this);

        // === Barra superior: transport + presets + estado del móvil ===
        addAndMakeVisible(powerButton_);
        powerButton_.setButtonText("Motor");
        powerButton_.setColour(juce::TextButton::buttonColourId, theme::primaryDark);
        powerButton_.onClick = [this] { toggleEngine(); };

        addAndMakeVisible(referenceButton_);
        referenceButton_.setButtonText("Capturar referencia");
        referenceButton_.onClick = [this] { engine_.captureReference(); };

        addAndMakeVisible(noiseButton_);
        noiseButton_.setButtonText("Capturar ruido");
        noiseButton_.onClick = [this] { engine_.startNoiseCapture(); };

        addAndMakeVisible(freezeButton_);
        freezeButton_.setButtonText("Congelar");
        freezeButton_.setClickingTogglesState(true);
        freezeButton_.setColour(juce::TextButton::buttonOnColourId, theme::eq2Amber);

        addAndMakeVisible(presetBox_);
        presetBox_.setTextWhenNothingSelected("Preset");
        presetBox_.onChange = [this] { loadSelectedPreset(); };

        addAndMakeVisible(savePresetButton_);
        savePresetButton_.setButtonText("Guardar");
        savePresetButton_.onClick = [this] { saveCurrentPreset(); };

        addAndMakeVisible(syncButton_);
        syncButton_.setButtonText("Sincronizar móvil");
        syncButton_.onClick = [this] { phoneLink_.requestSync(); };

        addAndMakeVisible(phoneLabel_);
        phoneLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        phoneLabel_.setColour(juce::Label::textColourId, theme::textDim);

        // === Los tres ecuas dinámicos, idénticos y seguidos ===
        for (int i = 0; i < 3; ++i) {
            DynamicEqCard::Snapshot snap;
            snap.title = juce::String("Ecu dinámico ") + juce::String(i + 1);
            snap.directionLabel = i == 0 ? "Donde más se necesita"
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            snap.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            cards_[i] = std::make_unique<DynamicEqCard>(snap);
            eqCardsPanel_.addAndMakeVisible(*cards_[i]);
        }
        addAndMakeVisible(eqCardsPanel_);

        // === EQ principal ===
        eqCanvas_ = std::make_unique<EqCanvas>([this](int band, float gain) {
            engine_.setBandGain(band, gain);
        });
        addAndMakeVisible(*eqCanvas_);

        // === Pestañas ===
        spectrumView_ = std::make_unique<SpectrumView>();
        routingMatrix_ = std::make_unique<RoutingMatrix>(deviceManager_);
        settingsPanel_ = std::make_unique<SettingsPanel>(engine_);
        tabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs_->addTab("EQ", theme::surface, eqCanvas_.get(), false);
        tabs_->addTab("Ecuas dinámicos", theme::surface, &eqCardsPanel_, false);
        tabs_->addTab("Ruteos", theme::surface, routingMatrix_.get(), false);
        tabs_->addTab("Ajustes", theme::surface, settingsPanel_.get(), false);
        tabs_->addTab("Análisis", theme::surface, spectrumView_.get(), false);
        addAndMakeVisible(*tabs_);

        refreshPresets();
        engine_.onAnalysis = [this](const acoustical::AnalysisResult& r) { storeAnalysis(r); };
        engine_.onSweep = [this](acoustical::SweepProcess p, const acoustical::SweepStep& s) {
            storeSweep(p, s);
        };

        startTimerHz(30);
        deviceManager_.addAudioCallback(this);

        // Vigilante USB: adb + sincronización con el móvil
        auto adb = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getParentDirectory().getChildFile("adb/adb.exe");
        phoneLink_ = std::make_unique<PhoneLink>(adb);
        phoneLink_->onSync = [this](const juce::var& payload) { applyPhoneSync(payload); };
        phoneLink_->startWatchdog();
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

    void audioDeviceIOCallback(const float** input, int numInputs, float** output,
                               int numOutputs, int numSamples) override {
        std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
        for (int ch = 0; ch < numInputs; ++ch)
            if (const auto* in = input[ch])
                for (int s = 0; s < numSamples; ++s) mono[s] += in[s];

        // Generador de señales → se procesa por el EQ como una entrada más
        if (generatorActive_.load() && numOutputs > 0) {
            generatorBuffer_.assign(static_cast<size_t>(numSamples), 0.0f);
            engine_.signalGenerator().fill(generatorBuffer_.data(), numSamples,
                                           static_cast<float>(lastSampleRate_));
            for (int s = 0; s < numSamples; ++s) mono[s] += generatorBuffer_[s];
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

        if (numOutputs > 0 && output[0])
            std::copy(left.begin(), left.end(), output[0]);
        if (numOutputs > 1 && output[1])
            std::copy(right.begin(), right.end(), output[1]);
        for (int ch = 2; ch < numOutputs; ++ch)
            if (output[ch])
                std::copy(right.begin(), right.end(), output[ch]);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        lastSampleRate_ = device ? device->getCurrentSampleRate() : 48000.0;
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const juce::AudioIODeviceCallbackContext&) override {}

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
            card.title = juce::String("Ecu dinámico ") + juce::String(i + 1);
            card.directionLabel = i == 0 ? "Donde más se necesita"
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
                    card.statusText = juce::String::formatted(
                        "%.0f Hz · %+.1f dB · suavizado %.0f ms",
                        sweepSteps_[i].centerFreqHz, sweepSteps_[i].gainDb,
                        sweepSteps_[i].smoothingMs);
                } else {
                    card.statusText = "Esperando señal…";
                }
            }
            cards_[i]->setSnapshot(card);
        }

        phoneLabel_.setText(phoneLink_ ? phoneLink_->lastSyncInfo() : juce::String(),
                            juce::dontSendNotification);
    }

    // === Presets (guardar/cargar/exportar) ===

    juce::File presetsDir() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("Presets");
    }

    juce::var serializeEngine() const {
        auto obj = new juce::DynamicObject();
        const auto& c = engine_.config();
        obj->setProperty("maxGainDb", c.maxGainDb);
        obj->setProperty("smoothingFactor", c.smoothingFactor);
        obj->setProperty("noiseFloorDb", c.noiseFloorDb);
        obj->setProperty("noiseSubtractionEnabled", c.noiseSubtractionEnabled);
        obj->setProperty("correctionEnabled", c.correctionEnabled);
        obj->setProperty("targetSpl", c.targetSpl);
        obj->setProperty("audioDelayMs", c.audioDelayMs);
        auto eqs = new juce::DynamicObject();
        for (int i = 0; i < 3; ++i) {
            auto eq = new juce::DynamicObject();
            eq->setProperty("enabled", dynamicEqEnabled_[i]);
            eq->setProperty("intervalMs", sweepInterval_[i]);
            eq->setProperty("speedMultiplier", sweepSpeed_[i]);
            eq->setProperty("extraSweeps", sweepExtras_[i]);
            eqs->setProperty("eq" + juce::String(i + 1), eq);
        }
        obj->setProperty("dynamicEqs", eqs);
        return juce::var(obj);
    }

    void applyPhoneSync(const juce::var& payload) {
        // Payload sincronizado desde la app móvil: perfiles, correcciones, ajustes
        auto* obj = payload.getDynamicObject();
        if (obj == nullptr) return;
        const auto cfg = obj->getProperty("config");
        auto* cfgObj = cfg.getDynamicObject();
        if (cfgObj != nullptr) {
            auto c = engine_.config();
            c.maxGainDb = static_cast<float>(static_cast<double>(cfgObj->getProperty("maxGainDb")));
            c.smoothingFactor = static_cast<float>(static_cast<double>(cfgObj->getProperty("smoothingFactor")));
            engine_.configure(c);
        }
        settingsPanel_->refresh();
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
        const auto v = juce::JSON::parse(f);
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return;
        auto c = engine_.config();
        c.maxGainDb = static_cast<float>(static_cast<double>(obj->getProperty("maxGainDb", c.maxGainDb)));
        c.smoothingFactor = static_cast<float>(static_cast<double>(obj->getProperty("smoothingFactor", c.smoothingFactor)));
        c.noiseFloorDb = static_cast<float>(static_cast<double>(obj->getProperty("noiseFloorDb", c.noiseFloorDb)));
        c.noiseSubtractionEnabled = static_cast<bool>(obj->getProperty("noiseSubtractionEnabled", c.noiseSubtractionEnabled));
        c.correctionEnabled = static_cast<bool>(obj->getProperty("correctionEnabled", c.correctionEnabled));
        c.targetSpl = static_cast<float>(static_cast<double>(obj->getProperty("targetSpl", c.targetSpl)));
        c.audioDelayMs = static_cast<float>(static_cast<double>(obj->getProperty("audioDelayMs", c.audioDelayMs)));
        engine_.configure(c);
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
        powerButton_.setBounds(top.removeFromLeft(84.0f));
        referenceButton_.setBounds(top.removeFromLeft(150.0f).reduced(2.0f, 0.0f));
        noiseButton_.setBounds(top.removeFromLeft(120.0f).reduced(2.0f, 0.0f));
        freezeButton_.setBounds(top.removeFromLeft(92.0f).reduced(2.0f, 0.0f));
        presetBox_.setBounds(top.removeFromLeft(160.0f).reduced(4.0f, 0.0f));
        savePresetButton_.setBounds(top.removeFromLeft(84.0f).reduced(2.0f, 0.0f));
        syncButton_.setBounds(top.removeFromLeft(150.0f).reduced(2.0f, 0.0f));
        phoneLabel_.setBounds(top);

        tabs_->setBounds(bounds);
    }

    juce::AudioDeviceManager& deviceManager_;
    acoustical::AcousticalEngine& engine_;

    juce::TextButton powerButton_, referenceButton_, noiseButton_, freezeButton_,
        savePresetButton_, syncButton_;
    juce::ComboBox presetBox_;
    juce::Label phoneLabel_;

    std::unique_ptr<juce::TabbedComponent> tabs_;
    std::unique_ptr<EqCanvas> eqCanvas_;
    std::array<std::unique_ptr<DynamicEqCard>, 3> cards_;
    juce::Component eqCardsPanel_;
    std::unique_ptr<SpectrumView> spectrumView_;
    std::unique_ptr<RoutingMatrix> routingMatrix_;
    std::unique_ptr<SettingsPanel> settingsPanel_;

    std::unique_ptr<PhoneLink> phoneLink_;

    // Estado de los ecuas dinámicos para la UI
    std::array<bool, 3> dynamicEqEnabled_{{true, false, false}};
    std::array<int, 3> sweepInterval_{{800, 800, 800}};
    std::array<float, 3> sweepSpeed_{{1.0f, 1.0f, 1.0f}};
    std::array<int, 3> sweepExtras_{{1, 1, 1}};
    std::array<acoustical::SweepStep, 3> sweepSteps_{};
    std::array<int, 3> activeBand_{{-1, -1, -1}};
    std::array<juce::String, 3> activeChannel_{{"L+R", "L+R", "L+R"}};

    std::mutex analysisMutex_;
    std::unique_ptr<acoustical::AnalysisResult> lastAnalysis_;
    std::mutex sweepMutex_;
    std::mutex dspMutex_;

    double lastSampleRate_ = 48000.0;
    std::vector<float> generatorBuffer_;
};
