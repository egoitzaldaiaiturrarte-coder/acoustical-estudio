// SpectrumView.h — RTA del espectro FFT con decaimiento, osciloscopio y
// medidor de SPL (instantáneo, pico y medio).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <AcousticalEngine.h>
#include "Theme.h"

class SpectrumView : public juce::Component {
public:
    void updateSpectrum(const acoustical::AnalysisResult& r) {
        std::lock_guard<std::mutex> lock(mutex_);
        spectrum_ = r.spectrum;
        spl_ = r.spl;
        peak_ = r.peakSpl;
        average_ = r.averageSpl;

        // Decaimiento: cada barra cae hacia el nuevo nivel sin saltos
        const int n = std::min<int>(static_cast<int>(r.measuredBandLevels.size()), 124);
        decay_.resize(n, -120.0f);
        for (int i = 0; i < n; ++i) {
            const float target = r.measuredBandLevels[i];
            decay_[i] += (target - decay_[i]) * 0.35f;
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(theme::background);
        g.fillRect(bounds);

        // === RTA (bandas logarítmicas con decaimiento) ===
        auto rta = bounds.removeFromTop(bounds.getHeight() * 0.62f).reduced(10.0f);
        g.setColour(theme::surface);
        g.fillRoundedRectangle(rta, 8.0f);

        std::vector<float> decay;
        std::vector<float> freqs = {100.0f, 1000.0f, 10000.0f};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            decay = decay_;
            freqs = spectrum_.frequencies.empty()
                ? freqs : spectrum_.frequencies;
        }
        if (!decay.empty()) {
            const float minDb = -90.0f, maxDb = 0.0f;
            const float w = rta.getWidth() / static_cast<float>(decay.size());
            for (int i = 0; i < static_cast<int>(decay.size()); ++i) {
                const float norm = juce::jmap(decay[i], minDb, maxDb, 0.0f, 1.0f);
                const float h = juce::jlimit(0.0f, rta.getHeight(), norm * rta.getHeight());
                // Color por posición logarítmica: graves fríos → agudos cálidos
                const float hue = juce::jlimit(0.0f, 1.0f,
                    static_cast<float>(i) / decay.size() * 0.75f);
                g.setColour(juce::Colour::fromHSV(0.55f - hue * 0.4f, 0.75f, 0.85f, 1.0f));
                g.fillRect(rta.getX() + i * w + 1.0f, rta.getBottom() - h, std::max(w - 2.0f, 1.0f), h);
            }
            // Marcas de frecuencia
            g.setColour(theme::textDim);
            g.setFont(10.0f);
            for (float mark : {100.0f, 1000.0f, 10000.0f}) {
                int nearest = 0;
                float best = 1e9f;
                for (int i = 0; i < static_cast<int>(freqs.size()); ++i) {
                    const float d = std::abs(std::log10(freqs[i] / mark));
                    if (d < best) { best = d; nearest = i; }
                }
                const float x = rta.getX() + rta.getWidth()
                    * static_cast<float>(nearest) / static_cast<float>(decay.size());
                g.drawText(mark >= 1000.0f ? juce::String(mark / 1000.0f, 0) + "k"
                                           : juce::String(mark, 0),
                           juce::Rectangle<float>(x - 16.0f, rta.getBottom() - 14.0f, 32.0f, 12.0f),
                           juce::Justification::centred);
            }
        }

        // === Medidor de SPL ===
        auto splArea = bounds.removeFromTop(bounds.getHeight() * 0.5f).reduced(10.0f, 4.0f);
        float spl = 0, peak = 0, avg = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            spl = spl_; peak = peak_; avg = average_;
        }
        g.setColour(theme::surface);
        g.fillRoundedRectangle(splArea, 8.0f);
        auto meter = splArea.removeFromLeft(splArea.getWidth() * 0.6f).reduced(12.0f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(meter, 4.0f);
        const float norm = juce::jlimit(0.0f, 1.0f, (spl - 30.0f) / 100.0f);
        g.setColour(norm > 0.85f ? theme::danger : theme::positive);
        g.fillRoundedRectangle(meter.removeFromLeft(meter.getWidth() * norm), 4.0f);

        g.setColour(theme::textPrimary);
        g.setFont(juce::Font(30.0f, juce::Font::bold));
        auto labels = splArea;
        g.drawText(juce::String(spl, 1) + " dB SPL", labels.removeFromTop(40.0f),
                   juce::Justification::centredLeft);
        g.setFont(juce::Font(13.0f));
        g.setColour(theme::textDim);
        g.drawText("Pico " + juce::String(peak, 1) + " · Medio " + juce::String(avg, 1),
                   labels, juce::Justification::centredLeft);
    }

private:
    std::mutex mutex_;
    acoustical::SpectrumFrame spectrum_;
    std::vector<float> decay_;
    float spl_ = 0.0f, peak_ = 0.0f, average_ = 0.0f;
};
