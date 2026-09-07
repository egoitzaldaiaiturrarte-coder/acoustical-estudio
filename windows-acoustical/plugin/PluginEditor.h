// PluginEditor.h — editor del plugin: EQ compacto con las bandas visibles y
// los tres mini-EQs de los ecuas dinámicos (cian, ámbar, magenta), igual que
// la app móvil. Reutiliza EqCanvas y DynamicEqCard de la app de escritorio.
#pragma once

#include "PluginProcessor.h"
#include "EqCanvas.h"
#include "DynamicEqCard.h"
#include "Theme.h"

class AcousticalEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AcousticalEditor(AcousticalAudioProcessor& p)
        : AudioProcessorEditor(p), processor_(p) {
        theme::apply(*this);

        eqCanvas_ = std::make_unique<EqCanvas>([this](int band, float gain) {
            processor_.engine().setBandGain(band, gain);
        });
        addAndMakeVisible(*eqCanvas_);

        for (int i = 0; i < 3; ++i) {
            DynamicEqCard::Snapshot snap;
            snap.title = juce::String("Ecu dinámico ") + juce::String(i + 1);
            snap.directionLabel = i == 0 ? "Donde más se necesita"
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            snap.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            cards_[i] = std::make_unique<DynamicEqCard>(snap);
            addAndMakeVisible(*cards_[i]);
        }
        startTimerHz(15);
        setSize(900, 560);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();
        const int cardsH = 150;
        auto cardsRow = bounds.removeFromBottom(cardsH);
        const int w = getWidth() / 3;
        for (int i = 0; i < 3; ++i)
            cards_[i]->setBounds(cardsRow.removeFromLeft(w).reduced(4));
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
            DynamicEqCard::Snapshot card;
            card.title = juce::String("Ecu dinámico ") + juce::String(i + 1);
            card.directionLabel = i == 0 ? "Donde más se necesita"
                                : i == 1 ? "Empezando por los graves"
                                         : "Empezando por los agudos";
            card.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            card.maxGainDb = processor_.engine().config().maxGainDb;
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
    std::unique_ptr<EqCanvas> eqCanvas_;
    std::array<std::unique_ptr<DynamicEqCard>, 3> cards_;
};
