// RoutingMatrix.h — estado de la tarjeta en uso (nombre, tasa, buffer,
// latencia), volumen general y sección Entradas: qué tarjeta escucha el
// micrófono de referencia y el nivel de cada canal de entrada. El selector
// completo de dispositivos (WASAPI/ASIO, canal por canal, panel ASIO) vive
// en la pestaña "Audio": es el mismo gestor, así que los dos se reflejan.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"
#include "RemoteAudioLink.h"

class RoutingMatrix : public juce::Component, private juce::Timer {
public:
    explicit RoutingMatrix(juce::AudioDeviceManager& dm, RemoteAudioLink& link)
        : deviceManager_(dm), link_(link) {
        addAndMakeVisible(statusLabel_);
        statusLabel_.setFont(juce::Font(13.0f));
        statusLabel_.setColour(juce::Label::textColourId, theme::textDim);
        statusLabel_.setJustificationType(juce::Justification::topLeft);

        // === Entradas: micrófono de referencia + nivel por canal ===
        addAndMakeVisible(*entradasGroup_);
        entradasGroup_->setColour(juce::GroupComponent::outlineColourId, theme::surfaceHi);
        entradasGroup_->addAndMakeVisible(refLabel_);
        refLabel_.setFont(juce::Font(13.0f));
        refLabel_.setColour(juce::Label::textColourId, theme::textDim);
        refLabel_.setText(juce::String::fromUTF8("Micrófono de referencia"),
                          juce::dontSendNotification);
        entradasGroup_->addAndMakeVisible(refBox_);
        refBox_.setTextWhenNothingSelected(juce::String::fromUTF8("Elegir tarjeta de entrada…"));
        refBox_.setTooltip(juce::String::fromUTF8("Qué tarjeta escucha el análisis (referencia, ruido, auto-alinear)"));
        refBox_.onChange = [this] { changeReferenceDevice(); };
        entradasGroup_->addAndMakeVisible(gainLLabel_);
        gainLLabel_.setFont(juce::Font(13.0f));
        gainLLabel_.setColour(juce::Label::textColourId, theme::textDim);
        gainLLabel_.setText(juce::String::fromUTF8("Nivel entrada L"), juce::dontSendNotification);
        entradasGroup_->addAndMakeVisible(gainL_);
        gainL_.setRange(0.0, 1.0, 0.01);
        gainL_.setValue(1.0, juce::dontSendNotification);
        gainL_.setTooltip(juce::String::fromUTF8("Ganancia del canal de entrada 1, antes del análisis"));
        gainL_.onValueChange = [this] {
            inputGainL_.store(static_cast<float>(gainL_.getValue()),
                              std::memory_order_relaxed);
        };
        entradasGroup_->addAndMakeVisible(gainRLabel_);
        gainRLabel_.setFont(juce::Font(13.0f));
        gainRLabel_.setColour(juce::Label::textColourId, theme::textDim);
        gainRLabel_.setText(juce::String::fromUTF8("Nivel entrada R"), juce::dontSendNotification);
        entradasGroup_->addAndMakeVisible(gainR_);
        gainR_.setRange(0.0, 1.0, 0.01);
        gainR_.setValue(1.0, juce::dontSendNotification);
        gainR_.setTooltip(juce::String::fromUTF8("Ganancia del canal de entrada 2, antes del análisis"));
        gainR_.onValueChange = [this] {
            inputGainR_.store(static_cast<float>(gainR_.getValue()),
                              std::memory_order_relaxed);
        };
        // Móviles en vivo (M2): una fila por micrófono remoto
        entradasGroup_->addAndMakeVisible(phonesPanel_);
        rebuildRefBox(true);

        // === Volumen general (salida) ===
        addAndMakeVisible(gainSlider_);
        gainSlider_.setRange(0.0, 1.0, 0.01);
        gainSlider_.setValue(mainGain_.load(), juce::dontSendNotification);
        gainSlider_.setTextValueSuffix(juce::String::fromUTF8(" · volumen general"));
        gainSlider_.onValueChange = [this] {
            // Atómico: lo lee el callback de audio en cada bloque (antes se
            // guardaba en un float sin usar nunca — control muerto, M9).
            mainGain_.store(static_cast<float>(gainSlider_.getValue()),
                            std::memory_order_relaxed);
        };
        addAndMakeVisible(gainLabel_);
        gainLabel_.attachToComponent(&gainSlider_, true);
        gainLabel_.setText("Volumen", juce::dontSendNotification);

        startTimerHz(4);
    }

    ~RoutingMatrix() override { stopTimer(); }

    // Ganancia de salida (0…1): la aplica el callback de audio de la app.
    float gain() const { return mainGain_.load(std::memory_order_relaxed); }
    // Niveles de entrada L/R (0…1): los aplica el callback antes del motor.
    float inputGainL() const { return inputGainL_.load(std::memory_order_relaxed); }
    float inputGainR() const { return inputGainR_.load(std::memory_order_relaxed); }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();
        statusLabel_.setBounds(bounds.removeFromTop(44.0f).reduced(12, 6));
        auto gainRow = bounds.removeFromBottom(48.0f).reduced(120, 8);
        gainSlider_.setBounds(gainRow);
        entradasGroup_->setBounds(bounds.removeFromTop(
            150.0f + 36.0f * static_cast<double>(phoneRows_.size())).reduced(12, 4));
        const auto g = entradasGroup_->getLocalBounds();
        refLabel_.setBounds(12, 34, 130, 22);
        refBox_.setBounds(146, 32, g.getWidth() - 158, 26);
        gainLLabel_.setBounds(12, 72, 130, 22);
        gainL_.setBounds(146, 70, g.getWidth() - 158, 26);
        gainRLabel_.setBounds(12, 108, 130, 22);
        gainR_.setBounds(146, 106, g.getWidth() - 158, 26);
        // Móviles (M2): filas bajo los niveles L/R
        phonesPanel_.setBounds(12, 140, g.getWidth() - 24,
                               36.0 * static_cast<double>(phoneRows_.size()));
        for (size_t i = 0; i < phoneRows_.size(); ++i)
            phoneRows_[i]->setBounds(0, static_cast<int>(i * 36.0),
                                     phonesPanel_.getWidth(), 32);
    }

private:
    // === Móviles (M2): micrófonos remotos como entradas del análisis ===
    // Una fila por móvil visto (aparece/desaparece sola): nombre, botón de
    // micrófono (el PC le pide al móvil que emita), ganancia y nivel.
    class PhoneRow : public juce::Component {
    public:
        PhoneRow(RemoteAudioLink& link, const juce::String& ip)
            : link_(link), ip_(ip) {
            addAndMakeVisible(label_);
            label_.setFont(juce::Font(13.0f));
            label_.setColour(juce::Label::textColourId, theme::textDim);
            label_.setTooltip(juce::String::fromUTF8(
                "Micrófono del móvil: al activarlo, suena en el PC como una entrada "
                "más (pasa por el análisis y los ecuas)"));

            addAndMakeVisible(micButton_);
            micButton_.setButtonText("Mic OFF");
            micButton_.setClickingTogglesState(true);
            micButton_.setColour(juce::TextButton::buttonOnColourId, theme::eq1Cyan);
            micButton_.setTooltip(juce::String::fromUTF8(
                "Pedir al móvil que emita su micrófono hacia el PC (y parar)"));
            micButton_.onClick = [this] {
                if (link_.setMicOn(ip_, !link_.micOn(ip_))) refresh();
            };

            addAndMakeVisible(gain_);
            gain_.setRange(0.0, 2.0, 0.01);
            gain_.setValue(link_.micGain(ip_), juce::dontSendNotification);
            gain_.setTextValueSuffix(juce::String::fromUTF8(" · nivel del móvil"));
            gain_.setTooltip(juce::String::fromUTF8(
                "Ganancia del micrófono del móvil antes del análisis"));
            gain_.onValueChange = [this] {
                link_.setMicGain(ip_, static_cast<float>(gain_.getValue()));
            };

            addAndMakeVisible(levelLabel_);
            levelLabel_.setFont(juce::Font(12.0f));
            levelLabel_.setColour(juce::Label::textColourId, theme::textDim);

            refresh();
        }

        void resized() override {
            auto b = getLocalBounds();
            label_.setBounds(b.removeFromLeft(300));
            levelLabel_.setBounds(b.removeFromRight(90));
            micButton_.setBounds(b.removeFromLeft(90).withHeight(b.getHeight()));
            gain_.setBounds(b.reduced(0, 4));
        }

        void refresh() {
            const auto* d = link_.deviceByIp(ip_);
            const auto name = d != nullptr ? RemoteAudioLink::displayName(*d)
                                           : juce::String::fromUTF8("Móvil ") + ip_;
            label_.setText(name, juce::dontSendNotification);
            const bool on = link_.micOn(ip_);
            micButton_.setToggleState(on, juce::dontSendNotification);
            micButton_.setButtonText(on ? "Mic ON" : "Mic OFF");
            gain_.setValue(link_.micGain(ip_), juce::dontSendNotification);
            const float db = link_.micLevelDb(ip_);
            levelLabel_.setText(db < -119.0f
                ? juce::String::fromUTF8("sin señal")
                : juce::String(db, 1) + juce::String::fromUTF8(" dB"),
                juce::dontSendNotification);
        }

    private:
        RemoteAudioLink& link_;
        juce::String ip_;
        juce::Label label_, levelLabel_;
        juce::ToggleButton micButton_;
        juce::Slider gain_;
    };

    // Reconstruye las filas cuando el conjunto de móviles en vivo cambia.
    void rebuildPhoneRows() {
        const auto live = link_.liveDevices();
        if (live.size() == phoneRows_.size()) return;
        for (auto& r : phoneRows_) phonesPanel_.removeChildComponent(r.get());
        phoneRows_.clear();
        for (const auto& d : live) {
            auto row = std::make_unique<PhoneRow>(link_, d.ip);
            phonesPanel_.addAndMakeVisible(*row);
            phoneRows_.push_back(std::move(row));
        }
        resized();   // vuelve a calcular la altura del grupo
    }

    // Nombres de las entradas que ofrece el tipo de dispositivo actual
    // (WASAPI/ASIO…). Vacío si no hay tipo activo.
    juce::StringArray inputDeviceNames() const {
        if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
            return type->getDeviceNames(true);
        return {};
    }

    void changeReferenceDevice() {
        const auto names = inputDeviceNames();
        const int idx = refBox_.getSelectedItemIndex();
        if (idx < 0 || idx >= names.size())
            return;
        // Solo cambia la entrada: salida, tasa y buffer se conservan.
        auto setup = deviceManager_.getAudioDeviceSetup();
        setup.inputDeviceName = names[idx];
        const juce::String err = deviceManager_.setAudioDeviceSetup(setup, true);
        if (err.isNotEmpty()) {
            statusLabel_.setText(juce::String::fromUTF8("No se pudo cambiar la entrada: ") + err,
                                 juce::dontSendNotification);
            rebuildRefBox(true);  // el combo vuelve a mostrar el estado real
        }
    }

    // Lista las entradas disponibles y deja marcada la que usa el PC ahora.
    // Solo reconstruye si la lista o el dispositivo cambiaron (el timer
    // pregunta 4 veces por segundo; si se enchufa una tarjeta, se refresca
    // sola, como hace el selector de la pestaña Audio).
    void rebuildRefBox(bool force) {
        const auto setup = deviceManager_.getAudioDeviceSetup();
        const juce::String current = setup.inputDeviceName;
        const auto names = inputDeviceNames();
        juce::String sig;
        for (const auto& n : names) sig << n << ";";
        if (!force && current == lastDeviceName_ && sig == lastInputList_)
            return;
        lastDeviceName_ = current;
        lastInputList_ = sig;
        refBox_.clear(juce::dontSendNotification);
        refBox_.addItemList(names, 1);  // ids 1…N, como en el resto de la app
        int selId = 0;
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == current) selId = i + 1;
        refBox_.setSelectedId(selId, juce::dontSendNotification);
    }

    void timerCallback() override {
        // Móviles (M2): las filas aparecen/desaparecen solas según los vivos
        rebuildPhoneRows();
        for (auto& r : phoneRows_) r->refresh();

        if (auto* d = deviceManager_.getCurrentAudioDevice()) {
            const double sr = d->getCurrentSampleRate();
            const int bs = d->getCurrentBufferSizeSamples();
            const double latency = d->getOutputLatencyInSamples() + d->getInputLatencyInSamples();
            statusLabel_.setText(juce::String::fromUTF8("Dispositivo: ") + d->getName()
                + juce::String::fromUTF8(" · ") + juce::String(sr / 1000.0, 0)
                + juce::String::fromUTF8(" kHz · buffer ") + juce::String(bs)
                + juce::String::fromUTF8(" muestras · latencia total ")
                + juce::String(latency / sr * 1000.0, 1) + juce::String::fromUTF8(" ms"),
                juce::dontSendNotification);
            rebuildRefBox(false);
        } else {
            statusLabel_.setText(juce::String::fromUTF8("Sin dispositivo: elige entrada y salida "
                "en la pestaña Audio (ASIO \"Acoustical Bridge\" para Cubase 5)."),
                juce::dontSendNotification);
            rebuildRefBox(false);
        }
    }

    juce::AudioDeviceManager& deviceManager_;
    juce::Label statusLabel_, gainLabel_;
    juce::Slider gainSlider_;
    std::atomic<float> mainGain_{1.0f};

    std::unique_ptr<juce::GroupComponent> entradasGroup_{
        new juce::GroupComponent(juce::String::fromUTF8("Entradas"),
                                 juce::String::fromUTF8("Entradas"))};
    juce::Label refLabel_, gainLLabel_, gainRLabel_;
    juce::ComboBox refBox_;
    juce::Slider gainL_, gainR_;
    std::atomic<float> inputGainL_{1.0f}, inputGainR_{1.0f};
    juce::String lastDeviceName_, lastInputList_;

    // Móviles en vivo (M2)
    RemoteAudioLink& link_;
    juce::Component phonesPanel_;
    std::vector<std::unique_ptr<PhoneRow>> phoneRows_;
};
