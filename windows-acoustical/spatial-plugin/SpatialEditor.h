// SpatialEditor.h — editor del "Acoustical Spatial" (v1.11.0): el panel
// grande y cómodo de la posición espacial — el "pantallazo" del móvil
// (SpatialJoystick.kt / ControlScreen) en ventana de DAW:
//
//   [Joystick x/y grande]   Entorno      [combo]
//                            Distancia    [fader 0–15 m]  7,5 m
//                            Tamaño       [fader]         Mediano
//                            Posición     Centro
//                            Compensación 0 dB · 0 ms · 7,5 m
//
//  - El joystick escribe en el APVTS (spatialX/Y) y sigue a los parámetros
//    cuando no se le arrastra (recall del DAW, automatización).
//  - Los faders y el combo van por attachments del APVTS (el estado del
//    proyecto y la automatización se mantienen, como en el VST principal).
//  - La compensación (ganancia + retardo) se calcula con la MISMA ley del
//    core C que aplica el audio: es la lectura que el usuario ve mientras
//    mueve el panel.
#pragma once

#include "SpatialProcessor.h"
#include "SpatialJoystick.h"
#include "Theme.h"

namespace {
juce::String utf8(const char* s) { return juce::String::fromUTF8(s); }
}

// Fila etiqueta + slider enlazada a un parámetro del APVTS (mismo patrón
// que la ParamRow del VST principal). El slider lleva su caja de valor a la
// derecha y la fila, una etiqueta de unidades fija ("m", "0–1").
namespace spatial {

class FaderRow : public juce::Component {
public:
    FaderRow(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
             const juce::String& text, const juce::String& unit)
        : label(text, text), unitLabel_(unit, unit) {
        label.setFont(juce::Font(11.0f));
        label.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(label);
        unitLabel_.setFont(juce::Font(11.0f));
        unitLabel_.setColour(juce::Label::textColourId, theme::textDim);
        unitLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(unitLabel_);

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
        label.setBounds(bounds.removeFromLeft(90));
        unitLabel_.setBounds(bounds.removeFromRight(44));
        slider.setBounds(bounds.reduced(0, 2));
    }

private:
    juce::Label label, unitLabel_;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class EnvCombo : public juce::Component {
public:
    EnvCombo(juce::AudioProcessorValueTreeState& state)
        : label(utf8("Entorno"), utf8("Entorno")) {
        label.setFont(juce::Font(11.0f));
        label.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(label);
        combo.setColour(juce::ComboBox::backgroundColourId, theme::surface);
        combo.setColour(juce::ComboBox::textColourId, theme::textPrimary);
        combo.setColour(juce::ComboBox::arrowColourId, theme::textDim);
        combo.addItem(utf8("Sala (8×10 m)"), 1);
        combo.addItem(utf8("Estudio (6×7 m)"), 2);
        combo.addItem(utf8("Escenario (12×8 m)"), 3);
        combo.addItem(utf8("Exterior (20×30 m)"), 4);
        addAndMakeVisible(combo);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "environment", combo);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        label.setBounds(bounds.removeFromLeft(90));
        combo.setBounds(bounds);
    }

private:
    juce::Label label;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

} // namespace spatial

class AcousticalSpatialEditor : public juce::AudioProcessorEditor,
                                private juce::Timer {
public:
    explicit AcousticalSpatialEditor(AcousticalSpatialProcessor& p)
        : AudioProcessorEditor(p), processor_(p),
          joy_(std::make_unique<SpatialJoystick>([this](float x, float y) {
              if (auto* px = processor_.parameters().getParameter("spatialX"))
                  px->setValueNotifyingHost(px->convertTo0to1(x));
              if (auto* py = processor_.parameters().getParameter("spatialY"))
                  py->setValueNotifyingHost(py->convertTo0to1(y));
          })) {
        theme::apply(*this);

        title_.setText(utf8("Acoustical Spatial — posición del elemento"),
                         juce::dontSendNotification);
        title_.setFont(juce::Font(15.0f, juce::Font::bold));
        title_.setColour(juce::Label::textColourId, theme::textPrimary);
        addAndMakeVisible(title_);

        dist_.reset(new spatial::FaderRow(
            processor_.parameters(), "spatialZ", utf8("Distancia (z)"),
            utf8("m")));
        size_.reset(new spatial::FaderRow(
            processor_.parameters(), "spatialSize", utf8("Tamaño"),
            utf8("0–1")));
        env_.reset(new spatial::EnvCombo(processor_.parameters()));
        addAndMakeVisible(*dist_);
        addAndMakeVisible(*size_);
        addAndMakeVisible(*env_);
        addAndMakeVisible(*joy_);

        posLabel_.setFont(juce::Font(11.0f));
        posLabel_.setColour(juce::Label::textColourId, theme::textPrimary);
        posLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(posLabel_);

        readout_.setFont(juce::Font(11.0f));
        readout_.setColour(juce::Label::textColourId, theme::textDim);
        readout_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(readout_);

        startTimerHz(15);
        setSize(680, 430);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        title_.setBounds(bounds.removeFromTop(28));
        auto body = bounds.reduced(0, 4);
        joy_->setBounds(body.removeFromLeft(350));
        auto right = body.reduced(10, 24);
        env_->setBounds(right.removeFromTop(34).reduced(0, 2));
        dist_->setBounds(right.removeFromTop(34).reduced(0, 2));
        size_->setBounds(right.removeFromTop(34).reduced(0, 2));
        posLabel_.setBounds(right.removeFromTop(26).reduced(0, 2));
        readout_.setBounds(right.removeFromTop(30));
    }

private:
    void timerCallback() override {
        const auto& P = processor_.parameters();
        const float x = P.getRawParameterValue("spatialX")->load();
        const float y = P.getRawParameterValue("spatialY")->load();
        const float z = P.getRawParameterValue("spatialZ")->load();
        const float size = P.getRawParameterValue("spatialSize")->load();
        const int envIdx = static_cast<int>(
            std::lround(P.getRawParameterValue("environment")->load()));
        const SpatialEnvironment env =
            (envIdx >= 0 && envIdx <= 3) ? static_cast<SpatialEnvironment>(envIdx)
                                          : SpatialEnvironment::Room;

        // El joystick sigue a los parámetros cuando no lo arrastra el
        // usuario (recall del DAW, automatización, otros editores).
        if (!joy_->isDragging())
            joy_->setPositionFromOutside(x, y);
        if (joy_->getEnvironment() != env)
            joy_->setEnvironment(env);

        // Lecturas en vivo (misma ley del core C que aplica el audio).
        posLabel_.setText(
            spatialPositionLabel(x, y, env), juce::dontSendNotification);
        float g = 0.0f, d = 0.0f;
        acoustical_spatial_compensate(x, y, z, size, &g, &d);
        const juce::String meters = juce::String(z * 15.0f, 1) + utf8(" m");
        readout_.setText(
            (g != 0.0f || d > 0.0f)
                ? utf8("Espacio: ") + juce::String(g, 1) + utf8(" dB de ganancia · ")
                  + juce::String(d, 1) + utf8(" ms de retardo · ") + meters
                  + utf8(" de distancia")
                : utf8("Posición neutra (0 dB · 0 ms) — el audio pasa igual"),
            juce::dontSendNotification);
    }

    AcousticalSpatialProcessor& processor_;
    juce::Label title_;
    std::unique_ptr<SpatialJoystick> joy_;
    std::unique_ptr<spatial::FaderRow> dist_, size_;
    std::unique_ptr<spatial::EnvCombo> env_;
    juce::Label posLabel_, readout_;
};
