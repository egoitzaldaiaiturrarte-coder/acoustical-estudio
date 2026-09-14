// HubPanel.h — pestaña "Hub": mezclador unificado del sistema. Una fila por
// ruta (la salida principal + hasta 4 auxiliares: Bluetooth, HDMI, USB, móvil…),
// cada una con activación, mute, inversión de fase, all-pass de alineación,
// ganancia, retardo editable en pasos de 0,01 ms, medidor de nivel y
// auto-alineación por micro (correlación cruzada contra la ruta principal).
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"
#include "RouteHub.h"

class HubPanel : public juce::Component, private juce::Timer {
public:
    explicit HubPanel(RouteHub& hub) : hub_(hub) {
        addAndMakeVisible(header_);
        header_.setFont(juce::Font(14.0f, juce::Font::bold));
        header_.setColour(juce::Label::textColourId, theme::textPrimary);
        header_.setText("Hub del sistema · la salida principal se replica en "
                        "todas las rutas activas, cada una con su fase, retardo "
                        "y correcciones", juce::dontSendNotification);

        addAndMakeVisible(refreshButton_);
        refreshButton_.setButtonText("Buscar dispositivos");
        refreshButton_.onClick = [this] { refreshDevices(); };

        addAndMakeVisible(hintLabel_);
        hintLabel_.setFont(juce::Font(12.0f));
        hintLabel_.setColour(juce::Label::textColourId, theme::textDim);
        hintLabel_.setJustificationType(juce::Justification::topLeft);
        hintLabel_.setText("Auto-alinear: reproduce música, ponte donde quieras "
                           "el punto dulce y pulsa \"Medir\" en cada ruta (necesita "
                           "un micrófono en la entrada del PC).", juce::dontSendNotification);

        for (int i = 0; i < RouteHub::NUM_ROUTES; ++i)
            rows_[i] = std::make_unique<RouteRow>(hub_, i);

        inner_.setSize(1320, RouteHub::NUM_ROUTES * ROW_H + 8);
        for (auto& row : rows_) inner_.addAndMakeVisible(row.get());
        viewport_.setViewedComponent(&inner_, false);
        addAndMakeVisible(viewport_);

        startTimerHz(30);
        refreshDevices();
    }

    ~HubPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();
        header_.setBounds(bounds.removeFromTop(26).reduced(12, 2));
        auto topRow = bounds.removeFromTop(30).reduced(12, 0);
        refreshButton_.setBounds(topRow.removeFromLeft(160));
        hintLabel_.setBounds(topRow.removeFromRight(getWidth() - 190).reduced(8, 0));
        viewport_.setBounds(bounds.reduced(8, 4));
        inner_.setSize(std::max(viewport_.getMaximumVisibleWidth(), 900),
                       RouteHub::NUM_ROUTES * ROW_H + 8);
        for (int i = 0; i < RouteHub::NUM_ROUTES; ++i)
            rows_[static_cast<size_t>(i)]->setBounds(
                4, i * ROW_H + 4, inner_.getWidth() - 8, ROW_H - 8);
    }

private:
    static constexpr int ROW_H = 118;

    // Medidor de nivel minimalista
    class LevelBar : public juce::Component {
    public:
        void setLevel(float l) { level_ = juce::jlimit(0.0f, 1.0f, l); }
        void paint(juce::Graphics& g) override {
            auto b = getLocalBounds().toFloat();
            g.setColour(theme::surface);
            g.fillRoundedRectangle(b, 3.0f);
            if (level_ > 0.003f) {
                auto bar = b.removeFromLeft(b.getWidth() * level_);
                g.setColour(level_ > 0.92f ? theme::eq3Magenta : theme::eq1Cyan);
                g.fillRoundedRectangle(bar, 3.0f);
            }
            g.setColour(theme::textDim.withAlpha(0.35f));
            g.drawRoundedRectangle(b, 3.0f, 1.0f);
        }
    private:
        float level_ = 0.0f;
    };

    // Una fila = una ruta completa
    class RouteRow : public juce::Component {
    public:
        RouteRow(RouteHub& hub, int index) : hub_(hub), index_(index) {
            const bool isMain = index_ == 0;
            addAndMakeVisible(title_);
            title_.setFont(juce::Font(13.0f, juce::Font::bold));
            title_.setColour(juce::Label::textColourId, isMain ? theme::eq1Cyan : theme::textPrimary);
            title_.setText(isMain ? juce::String("Ruta 1 · Salida principal")
                                  : juce::String("Ruta ") + juce::String(index_ + 1),
                           juce::dontSendNotification);

            if (!isMain) {
                addAndMakeVisible(deviceBox_);
                deviceBox_.setTextWhenNothingSelected("Elegir dispositivo de salida…");
                deviceBox_.onChange = [this] {
                    connectSelectedDevice();
                };
                addAndMakeVisible(connectButton_);
                connectButton_.setButtonText("Conectar");
                connectButton_.onClick = [this] { connectSelectedDevice(); };
            }

            addAndMakeVisible(activeToggle_);
            activeToggle_.setButtonText("Activa");
            activeToggle_.setToggleState(hub_.isRouteEnabled(index_), juce::dontSendNotification);
            activeToggle_.onStateChange = [this] {
                hub_.setRouteEnabled(index_, activeToggle_.getToggleState());
            };

            addAndMakeVisible(muteToggle_);
            muteToggle_.setButtonText("Mute");
            muteToggle_.setColour(juce::ToggleButton::textColourId, theme::textDim);
            muteToggle_.onStateChange = [this] {
                hub_.setRouteMute(index_, muteToggle_.getToggleState());
            };

            addAndMakeVisible(phaseToggle_);
            phaseToggle_.setButtonText("Fase ⊖ (invertir)");
            phaseToggle_.setColour(juce::ToggleButton::textColourId, theme::textDim);
            phaseToggle_.onStateChange = [this] {
                hub_.setPhaseInvert(index_, phaseToggle_.getToggleState());
            };

            addAndMakeVisible(allPassBox_);
            allPassBox_.addItem("Fase: sin all-pass", 1);
            allPassBox_.addItem("All-pass 60 Hz", 2);
            allPassBox_.addItem("All-pass 80 Hz", 3);
            allPassBox_.addItem("All-pass 100 Hz", 4);
            allPassBox_.addItem("All-pass 120 Hz", 5);
            allPassBox_.setSelectedId(1, juce::dontSendNotification);
            allPassBox_.onChange = [this] {
                const int id = allPassBox_.getSelectedId();
                if (id <= 1) hub_.setAllPass(index_, false, 80.0f);
                else hub_.setAllPass(index_, true, id == 2 ? 60.0f : id == 3 ? 80.0f
                                                    : id == 4 ? 100.0f : 120.0f);
            };

            addAndMakeVisible(gainSlider_);
            gainSlider_.setRange(-24.0, 6.0, 0.1);
            gainSlider_.setValue(hub_.routeGainDb(index_), juce::dontSendNotification);
            gainSlider_.setTextValueSuffix(" dB");
            gainSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 76, 22);
            gainSlider_.onValueChange = [this] {
                hub_.setRouteGainDb(index_, static_cast<float>(gainSlider_.getValue()));
            };
            addAndMakeVisible(gainLabel_);
            gainLabel_.setText("Ganancia", juce::dontSendNotification);
            gainLabel_.attachToComponent(&gainSlider_, true);
            gainLabel_.setFont(juce::Font(12.0f));
            gainLabel_.setColour(juce::Label::textColourId, theme::textDim);

            addAndMakeVisible(delaySlider_);
            delaySlider_.setRange(0.0, RouteHub::MAX_DELAY_MS, 0.01);
            delaySlider_.setValue(hub_.delayMs(index_), juce::dontSendNotification);
            delaySlider_.setTextValueSuffix(" ms");
            delaySlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 84, 22);
            delaySlider_.onValueChange = [this] {
                hub_.setDelayMs(index_, static_cast<float>(delaySlider_.getValue()));
            };
            addAndMakeVisible(delayLabel_);
            delayLabel_.setText("Retardo", juce::dontSendNotification);
            delayLabel_.attachToComponent(&delaySlider_, true);
            delayLabel_.setFont(juce::Font(12.0f));
            delayLabel_.setColour(juce::Label::textColourId, theme::textDim);

            addAndMakeVisible(measureButton_);
            measureButton_.setButtonText("Medir");
            measureButton_.onClick = [this] {
                hub_.startAlignment(index_);
                status_.setText("Midiendo… deja la música sonando",
                                juce::dontSendNotification);
            };

            addAndMakeVisible(status_);
            status_.setFont(juce::Font(12.0f));
            status_.setColour(juce::Label::textColourId, theme::textDim);

            addAndMakeVisible(levelBar_);
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(10, 6);
            auto top = bounds.removeFromTop(26);
            title_.setBounds(top.removeFromLeft(190));
            if (deviceBox_.getParentComponent() == this) {
                deviceBox_.setBounds(top.removeFromLeft(300).reduced(4, 0));
                connectButton_.setBounds(top.removeFromLeft(90).reduced(4, 0));
            }
            top.removeFromLeft(8);
            activeToggle_.setBounds(top.removeFromLeft(78));
            muteToggle_.setBounds(top.removeFromLeft(70));
            phaseToggle_.setBounds(top.removeFromLeft(120));
            allPassBox_.setBounds(top.removeFromLeft(150).reduced(4, 0));
            levelBar_.setBounds(top.removeFromRight(180).reduced(0, 8));

            auto mid = bounds.removeFromTop(26);
            gainSlider_.setBounds(mid.removeFromLeft(300).reduced(4, 0));
            mid.removeFromLeft(10);
            delaySlider_.setBounds(mid.removeFromLeft(320).reduced(4, 0));
            mid.removeFromLeft(10);
            measureButton_.setBounds(mid.removeFromLeft(90));

            status_.setBounds(bounds.removeFromTop(20).reduced(2, 0));
        }

        // Refresca controles desde el hub (llamado por el temporizador)
        void syncFromHub() {
            activeToggle_.setToggleState(hub_.isRouteEnabled(index_), juce::dontSendNotification);
            levelBar_.setLevel(hub_.routeLevel(index_));
            levelBar_.repaint();
        }

        juce::ComboBox* deviceBox() { return deviceBox_.getParentComponent() == this ? &deviceBox_ : nullptr; }
        juce::TextButton* connectButton() { return connectButton_.getParentComponent() == this ? &connectButton_ : nullptr; }

        void setStatus(const juce::String& s) {
            status_.setText(s, juce::dontSendNotification);
        }

        juce::String statusText() const { return status_.getText(); }

        int selectedIndex() const {
            return deviceBox_.getParentComponent() == this ? deviceBox_.getSelectedId() : 0;
        }
        juce::String selectedDeviceName() const {
            return deviceBox_.getParentComponent() == this ? deviceBox_.getText() : juce::String();
        }

    private:
        void connectSelectedDevice() {
            const auto name = selectedDeviceName();
            if (name.isEmpty()) return;
            juce::String error;
            if (hub_.openRoute(index_, name, error)) {
                status_.setText("Conectado: " + name + " · "
                                + juce::String(hub_.routeInfo(index_).sampleRate, 0) + " Hz",
                                juce::dontSendNotification);
                connectButton_.setButtonText("Conectado");
            } else {
                status_.setText("No se pudo abrir: " + (error.isNotEmpty() ? error : name),
                                juce::dontSendNotification);
                connectButton_.setButtonText("Conectar");
            }
        }

        RouteHub& hub_;
        int index_;
        juce::Label title_;
        juce::ComboBox deviceBox_;
        juce::TextButton connectButton_;
        juce::ToggleButton activeToggle_, muteToggle_, phaseToggle_;
        juce::ComboBox allPassBox_;
        juce::Slider gainSlider_, delaySlider_;
        juce::Label gainLabel_, delayLabel_, status_;
        juce::TextButton measureButton_;
        LevelBar levelBar_;
    };

    void refreshDevices() {
        devices_ = hub_.availableOutputDevices();
        for (int i = 1; i < RouteHub::NUM_ROUTES; ++i) {
            if (auto* box = rows_[static_cast<size_t>(i)]->deviceBox()) {
                box->clear(juce::dontSendNotification);
                int id = 1;
                for (const auto& d : devices_)
                    box->addItem(d, id++);
                const auto open = hub_.routeInfo(i).name;
                if (open.isNotEmpty()) {
                    for (int k = 0; k < box->getNumItems(); ++k)
                        if (box->getItemText(k) == open) {
                            box->setSelectedItemIndex(k, juce::dontSendNotification);
                            break;
                        }
                }
            }
        }
    }

    void timerCallback() override {
        for (int i = 0; i < RouteHub::NUM_ROUTES; ++i) {
            auto& row = *rows_[static_cast<size_t>(i)];
            row.syncFromHub();

            // Estado y resultado de la medición
            if (hub_.alignmentStage() != 0 && hub_.alignmentRoute() == i) {
                float ms = 0.0f;
                const int result = hub_.pollAlignment(i, ms);
                if (result == 0) {
                    row.setStatus(hub_.alignmentStage() == 1
                        ? "Midiendo la ruta principal (referencia)…"
                        : "Midiendo esta ruta… deja la música sonando");
                } else if (result == 1) {
                    row.setStatus(i == 0 && !hub_.hasBaseline()
                                  ? juce::String("Referencia capturada: ya puedes medir el resto")
                                  : juce::String("Alineada: retardo ") + juce::String(hub_.delayMs(i), 2) + " ms");
                } else {
                    row.setStatus("Medición fallida: " + hub_.lastAlignmentError());
                }
            } else if (hub_.alignmentStage() == 0
                       && row.statusText().contains("Midiendo")) {
                row.setStatus({});
            }

            // Estado de conexión
            if (i > 0) {
                const auto info = hub_.routeInfo(i);
                if (info.open && !row.statusText().contains("Alineada")
                    && !row.statusText().contains("Conectado")
                    && !row.statusText().contains("No se pudo")
                    && !row.statusText().contains("Midiendo"))
                    row.setStatus("Conectado: " + info.name + " · "
                                  + juce::String(info.sampleRate, 0) + " Hz");
            }
        }
    }

    RouteHub& hub_;
    juce::Label header_, hintLabel_;
    juce::TextButton refreshButton_;
    juce::Viewport viewport_;
    juce::Component inner_;
    std::array<std::unique_ptr<RouteRow>, RouteHub::NUM_ROUTES> rows_{};
    juce::StringArray devices_;
};
