// DynamicEqCard.h — mini-EQ en vivo por cada ecu dinámico (cian, ámbar,
// magenta) con su curva de ganancia, banda activa y estado textual.
// Port del DynamicEqCanvas de la app móvil.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

class DynamicEqCard : public juce::Component, private juce::Timer {
public:
    struct Snapshot {
        juce::Colour accent = theme::eq1Cyan;
        juce::String title = "Ecu dinámico 1";
        juce::String directionLabel = "Donde más se necesita";
        juce::String statusText;
        bool enabled = false;
        std::vector<float> gains;
        int activeBand = -1;
        float maxGainDb = 12.0f;
        juce::String channelBadge;   // "L+R", "L" o "R"
    };

    explicit DynamicEqCard(Snapshot initial) : snapshot_(std::move(initial)) {
        startTimerHz(12);
        setBufferedToImage(false);
    }

    void setSnapshot(const Snapshot& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = s;
    }

    void paint(juce::Graphics& g) override {
        Snapshot snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = snap;
        }
        auto bounds = getLocalBounds().toFloat();
        g.setColour(theme::surface);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(snap.accent.withAlpha(snap.enabled ? 0.8f : 0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

        // Cabecera: título + estado
        auto header = bounds.removeFromTop(26.0f).reduced(10.0f, 4.0f);
        g.setColour(snap.enabled ? snap.accent : theme::textDim);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(snap.title, header.removeFromLeft(header.getWidth() * 0.5f),
                   juce::Justification::centredLeft);
        g.setFont(juce::Font(11.0f));
        g.setColour(theme::textDim);
        g.drawText(snap.directionLabel, header, juce::Justification::centredRight);

        // Zona de curva
        auto curveArea = bounds.reduced(10.0f, 6.0f).withTrimmedBottom(16.0f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(curveArea, 6.0f);

        const int n = static_cast<int>(snap.gains.size());
        const float zeroY = curveArea.getCentreY();
        const float half = curveArea.getHeight() / 2.0f - 4.0f;
        const float maxG = std::max(snap.maxGainDb, 1.0f);

        if (!snap.enabled) {
            g.setColour(theme::textDim);
            g.setFont(11.0f);
            g.drawText("Desactivado", curveArea, juce::Justification::centred);
        } else if (n > 1) {
            juce::Path path;
            for (int i = 0; i < n; ++i) {
                const float x = curveArea.getX()
                    + curveArea.getWidth() * static_cast<float>(i) / static_cast<float>(n - 1);
                const float gain = juce::jlimit(-snap.maxGainDb, snap.maxGainDb, snap.gains[i]);
                const float y = zeroY - (gain / maxG) * half;
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
            g.setColour(snap.accent);
            g.strokePath(path, juce::PathStrokeType(1.8f));

            if (snap.activeBand >= 0 && snap.activeBand < n) {
                const float x = curveArea.getX() + curveArea.getWidth()
                    * static_cast<float>(snap.activeBand) / static_cast<float>(n - 1);
                const float gain = juce::jlimit(-snap.maxGainDb, snap.maxGainDb,
                                                snap.gains[snap.activeBand]);
                g.setColour(juce::Colours::white);
                g.fillEllipse(x - 3.5f, zeroY - (gain / maxG) * half - 3.5f, 7.0f, 7.0f);
            }
        }

        // Pie: estado en vivo + badge de canal L/R
        auto footer = bounds.removeFromBottom(16.0f).reduced(10.0f, 2.0f);
        g.setFont(juce::Font(10.5f));
        g.setColour(theme::textDim);
        g.drawText(snap.statusText, footer, juce::Justification::centredLeft);
        if (snap.channelBadge.isNotEmpty()) {
            g.setColour(snap.accent);
            g.drawText(snap.channelBadge, footer, juce::Justification::centredRight);
        }
    }

private:
    void timerCallback() override { repaint(); }

    Snapshot snapshot_;
    std::mutex mutex_;
};
