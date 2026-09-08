// RoutingMatrix.h — gestión de tarjetas de audio: selector nativo de
// dispositivos (tipos WASAPI/ASIO/DirectSound, entradas/salidas, canales,
// muestreo y buffer, panel ASIO), estado en vivo y control de ganancia.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

class RoutingMatrix : public juce::Component, private juce::Timer {
public:
    explicit RoutingMatrix(juce::AudioDeviceManager& dm) : deviceManager_(dm) {
        selector_ = std::make_unique<juce::AudioDeviceSelectorComponent>(
            dm, 1, 2, 1, 8, true, true, true, false);
        addAndMakeVisible(*selector_);

        addAndMakeVisible(statusLabel_);
        statusLabel_.setFont(juce::Font(13.0f));
        statusLabel_.setColour(juce::Label::textColourId, theme::textDim);
        statusLabel_.setJustificationType(juce::Justification::topLeft);

        addAndMakeVisible(gainSlider_);
        gainSlider_.setRange(0.0, 1.0, 0.01);
        gainSlider_.setValue(mainGain_, juce::dontSendNotification);
        gainSlider_.setTextValueSuffix(" · volumen general");
        gainSlider_.onValueChange = [this] {
            mainGain_ = static_cast<float>(gainSlider_.getValue());
        };
        addAndMakeVisible(gainLabel_);
        gainLabel_.attachToComponent(&gainSlider_, true);
        gainLabel_.setText("Volumen", juce::dontSendNotification);

        startTimerHz(4);
    }

    ~RoutingMatrix() override { stopTimer(); }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();
        statusLabel_.setBounds(bounds.removeFromTop(44.0f).reduced(12, 6));
        auto gainRow = bounds.removeFromBottom(48.0f).reduced(120, 8);
        gainSlider_.setBounds(gainRow);
        selector_->setBounds(bounds.reduced(8));
    }

private:
    void timerCallback() override {
        if (auto* d = deviceManager_.getCurrentAudioDevice()) {
            const double sr = d->getCurrentSampleRate();
            const int bs = d->getCurrentBufferSizeSamples();
            const double latency = d->getOutputLatencyInSamples() + d->getInputLatencyInSamples();
            statusLabel_.setText(juce::String::formatted(
                "Dispositivo: %s · %.0f kHz · buffer %d muestras · latencia total %.1f ms",
                d->getName().toRawUTF8(), sr, bs, latency / sr * 1000.0),
                juce::dontSendNotification);
        } else {
            statusLabel_.setText("Sin dispositivo: elige entrada y salida arriba "
                                 "(ASIO \"Acoustical Bridge\" para Cubase 5).",
                                 juce::dontSendNotification);
        }
    }

    juce::AudioDeviceManager& deviceManager_;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector_;
    juce::Label statusLabel_, gainLabel_;
    juce::Slider gainSlider_;
    float mainGain_ = 1.0f;
};
