// PluginEditor.h — editor del plugin: EQ compacto con las bandas visibles,
// los tres mini-EQs de los ecuas dinámicos (cian, ámbar, magenta) y TODOS los
// parámetros editables desde la propia ventana, enlazados al APVTS (automatizables).
#pragma once

#include "PluginProcessor.h"
#include "EqCanvas.h"
#include "DynamicEqCard.h"
#include "Theme.h"

// Fila compacta etiqueta + slider, enlazada a un parámetro del APVTS.
class ParamRow : public juce::Component {
public:
    ParamRow(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
             const juce::String& text)
        : label(text, text) {
        label.setFont(juce::Font(11.0f));
        label.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 16);
        slider.setColour(juce::Slider::backgroundColourId, theme::background);
        slider.setColour(juce::Slider::trackColourId, theme::primaryDark);
        slider.setColour(juce::Slider::thumbColourId, theme::primary);
        slider.setColour(juce::Slider::textBoxTextColourId, theme::textPrimary);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, theme::surface);
        addAndMakeVisible(slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, paramId, slider);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        label.setBounds(bounds.removeFromLeft(78));
        slider.setBounds(bounds.reduced(0, 2));
    }

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Interruptor enlazado a un parámetro bool del APVTS.
class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& text)
        : juce::ToggleButton(text) {
        setColour(juce::ToggleButton::textColourId, theme::textPrimary);
        setColour(juce::ToggleButton::tickColourId, theme::primary);
        setColour(juce::ToggleButton::tickDisabledColourId, theme::textDim);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, paramId, *this);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Panel de controles de un ecu dinámico (n = "1".."3").
class EqControlsPanel : public juce::Component {
public:
    EqControlsPanel(juce::AudioProcessorValueTreeState& state, const juce::String& n)
        : activo(state, "eq" + n + "Enabled", "Activo"),
          intervalo(state, "eq" + n + "Interval", "Intervalo ms"),
          ganancia(state, "eq" + n + "Gain", "Ganancia dB"),
          mezclador(state, "eq" + n + "Mixer", "Mezclador"),
          velocidad(state, "eq" + n + "Speed", "Velocidad"),
          extras(state, "eq" + n + "Extras", "Extras") {
        addAndMakeVisible(activo);
        for (auto* row : { &intervalo, &ganancia, &mezclador, &velocidad, &extras })
            addAndMakeVisible(*row);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        activo.setBounds(bounds.removeFromTop(22).reduced(4, 0));
        for (auto* row : { &intervalo, &ganancia, &mezclador, &velocidad, &extras })
            row->setBounds(bounds.removeFromTop(24).reduced(4, 0));
    }

private:
    ParamToggle activo;
    ParamRow intervalo, ganancia, mezclador, velocidad, extras;
};

class AcousticalEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AcousticalEditor(AcousticalAudioProcessor& p)
        : AudioProcessorEditor(p), processor_(p),
          correccion_(p.parameters(), "correction", "Corrección"),
          link_(p.parameters(), "link", "Link L/R"),
          ruido_(p.parameters(), "noiseSub", "Sustracción de ruido"),
          maxGain_(p.parameters(), "maxGain", "Ganancia máx dB"),
          suavizado_(p.parameters(), "smoothing", "Suavizado") {
        theme::apply(*this);

        addAndMakeVisible(correccion_);
        addAndMakeVisible(link_);
        addAndMakeVisible(ruido_);
        addAndMakeVisible(maxGain_);
        addAndMakeVisible(suavizado_);

        referencia_.setButtonText("Capturar referencia");
        referencia_.setColour(juce::TextButton::buttonColourId, theme::surfaceHi);
        referencia_.setColour(juce::TextButton::textColourOffId, theme::textPrimary);
        referencia_.onClick = [this] { processor_.engine().captureReference(); };
        addAndMakeVisible(referencia_);

        eqCanvas_ = std::make_unique<EqCanvas>([this](int band, float gain) {
            processor_.engine().setBandGain(band, gain);
        });
        addAndMakeVisible(*eqCanvas_);

        for (int i = 0; i < 3; ++i) {
            const juce::String n = juce::String(i + 1);
            DynamicEqCard::Snapshot snap;
            snap.title = juce::String("Ecu dinámico ") + n;
            snap.directionLabel = i == 0 ? "Donde más se necesita"
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            snap.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            cards_[i] = std::make_unique<DynamicEqCard>(snap);
            addAndMakeVisible(*cards_[i]);

            panels_[i] = std::make_unique<EqControlsPanel>(processor_.parameters(), n);
            addAndMakeVisible(*panels_[i]);
        }
        startTimerHz(15);
        setSize(980, 740);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();

        // Barra maestra: corrección, link, ruido, referencia y dos sliders.
        auto top = bounds.removeFromTop(48).reduced(8, 4);
        correccion_.setBounds(top.removeFromLeft(104));
        link_.setBounds(top.removeFromLeft(80));
        ruido_.setBounds(top.removeFromLeft(150));
        referencia_.setBounds(top.removeFromLeft(150).reduced(0, 8));
        suavizado_.setBounds(top.removeFromRight(230));
        maxGain_.setBounds(top.removeFromRight(230));

        // Fila inferior: tarjeta + panel de controles por ecu dinámico.
        auto bottom = bounds.removeFromBottom(310);
        const int w = getWidth() / 3;
        for (int i = 0; i < 3; ++i) {
            auto col = bottom.removeFromLeft(w).reduced(4, 2);
            panels_[i]->setBounds(col.removeFromBottom(150));
            cards_[i]->setBounds(col.removeFromBottom(150));
        }
        eqCanvas_->setBounds(bounds.reduced(6));
    }

private:
    void timerCallback() override {
        acoustical::AnalysisResult analysis;
        acoustical::SweepStep sweeps[3];
        processor_.getLatestAnalysis(analysis, sweeps);

        EqCanvas::Snapshot snap;
        snap.bandFrequencies = processor_.engine().bandFrequencies();
        snap.gains = analysis.combinedGainsL;
        snap.gainsR = analysis.combinedGainsR;
        snap.maxGainDb = processor_.engine().config().maxGainDb;
        for (int i = 0; i < 3; ++i) {
            if (sweeps[i].bandIndex >= 0
                && sweeps[i].bandIndex < static_cast<int>(snap.bandFrequencies.size()))
                snap.highlights[sweeps[i].bandIndex] =
                    i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
        }
        eqCanvas_->setSnapshot(snap);

        for (int i = 0; i < 3; ++i) {
            const juce::String n = juce::String(i + 1);
            DynamicEqCard::Snapshot card;
            card.title = juce::String("Ecu dinámico ") + n;
            card.directionLabel = i == 0 ? "Donde más se necesita"
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            card.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            card.maxGainDb = processor_.engine().config().maxGainDb;
            card.enabled = processor_.parameters()
                               .getRawParameterValue("eq" + n + "Enabled")->load() > 0.5f;
            if (analysis.dynamicEqGainsL.size() > static_cast<size_t>(i))
                card.gains = analysis.dynamicEqGainsL[i];
            if (sweeps[i].bandIndex >= 0) {
                card.activeBand = sweeps[i].bandIndex;
                card.channelBadge = sweeps[i].channel;
                card.statusText = juce::String::formatted("%.0f Hz · %+.1f dB · suavizado %.0f ms",
                    sweeps[i].centerFreqHz, sweeps[i].gainDb, sweeps[i].smoothingMs);
            }
            cards_[i]->setSnapshot(card);
        }
    }

    AcousticalAudioProcessor& processor_;
    ParamToggle correccion_, link_, ruido_;
    ParamRow maxGain_, suavizado_;
    juce::TextButton referencia_;
    std::unique_ptr<EqCanvas> eqCanvas_;
    std::array<std::unique_ptr<DynamicEqCard>, 3> cards_;
    std::array<std::unique_ptr<EqControlsPanel>, 3> panels_;
};
