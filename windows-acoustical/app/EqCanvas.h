// EqCanvas.h — EQ compacto con las 124 bandas visibles a la vez, arrastre con
// el ratón, colores por ecu dinámico que trabaja en cada banda y marcador de
// la banda activa. Port del CompactEqCanvas + DynamicEqColors de la app móvil.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <AcousticalEngine.h>

class EqCanvas : public juce::Component {
public:
    struct Snapshot {
        std::vector<float> gains;                    // ganancia combinada por banda
        std::vector<float> bandFrequencies;
        std::map<int, juce::Colour> highlights;      // banda → color del ecu que la trabaja
        int activeBand = -1;
        juce::Colour activeColour = theme::eq1Cyan;
        float maxGainDb = 12.0f;
        bool stereoUnlinked = false;
        std::vector<float> gainsR;
    };

    explicit EqCanvas(std::function<void(int, float)> onBandChanged)
        : onBandChanged_(std::move(onBandChanged)) {
        setMouseClickGrabsKeyboardFocus(false);
        setBufferedToImage(true);
    }

    void setSnapshot(const Snapshot& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = s;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        Snapshot snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snap = snapshot_;
        }
        const auto bounds = getLocalBounds().toFloat().reduced(8.0f);
        const int n = static_cast<int>(snap.bandFrequencies.size());
        if (n == 0) {
            g.setColour(theme::textDim);
            g.drawText("Arranca el motor para ver las bandas", getLocalBounds(),
                       juce::Justification::centred);
            return;
        }

        const float w = bounds.getWidth() / static_cast<float>(n);
        const float zeroY = bounds.getCentreY();
        const float half = bounds.getHeight() / 2.0f - 6.0f;
        const float maxG = std::max(snap.maxGainDb, 1.0f);

        // Línea de 0 dB y marcas 100 / 1k / 10k
        g.setColour(theme::surfaceHi);
        g.fillRect(bounds.getX(), zeroY - 0.5f, bounds.getWidth(), 1.0f);
        g.setColour(theme::textDim);
        g.setFont(10.0f);
        for (float mark : {100.0f, 1000.0f, 10000.0f}) {
            int nearest = 0;
            float best = 1e9f;
            for (int i = 0; i < n; ++i) {
                const float d = std::abs(std::log10(snap.bandFrequencies[i] / mark));
                if (d < best) { best = d; nearest = i; }
            }
            const float x = bounds.getX() + (nearest + 0.5f) * w;
            g.setColour(theme::surfaceHi);
            g.fillRect(x, bounds.getY(), 1.0f, bounds.getHeight());
            g.setColour(theme::textDim);
            g.drawText(mark >= 1000.0f ? juce::String(mark / 1000.0f, 0) + "k"
                                       : juce::String(mark, 0),
                       juce::Rectangle<float>(x - 12.0f, bounds.getBottom() - 14.0f, 24.0f, 12.0f),
                       juce::Justification::centred);
        }

        // Faders finos, tintados con el color del ecu dinámico que los trabaja
        for (int i = 0; i < n; ++i) {
            const float gain = i < static_cast<int>(snap.gains.size()) ? snap.gains[i] : 0.0f;
            const float x = bounds.getX() + i * w;
            const float barW = std::max(w - std::max(w * 0.25f, 0.6f), 1.2f);

            auto colour = theme::primaryDark;
            const auto it = snap.highlights.find(i);
            if (it != snap.highlights.end()) colour = it->second;
            if (i == snap.activeBand) colour = juce::Colours::white;

            if (gain >= 0.0f) {
                const float h = (gain / maxG) * half;
                g.setColour(colour.withAlpha(0.9f));
                g.fillRoundedRectangle(x + w * 0.1f, zeroY - h, barW, std::max(h, 1.0f), 1.5f);
            } else {
                const float h = (-gain / maxG) * half;
                g.setColour(colour.withAlpha(0.65f));
                g.fillRoundedRectangle(x + w * 0.1f, zeroY, barW, std::max(h, 1.0f), 1.5f);
            }

            // Marcador circular en la cima de la banda activa
            if (i == snap.activeBand) {
                const float y = zeroY - (gain >= 0.0f ? (gain / maxG) * half
                                                      : -(-gain / maxG) * half);
                g.setColour(snap.activeColour);
                g.fillEllipse(x + w / 2.0f - 3.0f, y - 3.0f, 6.0f, 6.0f);
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        Snapshot snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snap = snapshot_;
        }
        const int n = static_cast<int>(snap.bandFrequencies.size());
        if (n == 0) return;
        const auto bounds = getLocalBounds().toFloat().reduced(8.0f);
        const float w = bounds.getWidth() / static_cast<float>(n);
        const int index = juce::jlimit(0, n - 1,
            static_cast<int>((e.position.x - bounds.getX()) / w));
        const float zeroY = bounds.getCentreY();
        const float half = bounds.getHeight() / 2.0f - 6.0f;
        const float gain = juce::jlimit(-snap.maxGainDb, snap.maxGainDb,
                                        -(e.position.y - zeroY) / half * snap.maxGainDb);
        if (onBandChanged_) onBandChanged_(index, gain);
    }

    void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }

    void mouseUp(const juce::MouseEvent& e) override {
        juce::ignoreUnused(e);
        // Etiqueta exacta de la banda bajo el ratón (como en el móvil)
    }

private:
    std::function<void(int, float)> onBandChanged_;
    Snapshot snapshot_;
    std::mutex mutex_;
};
