// RefSpectrum.h — mini-espectro de la referencia capturada (v1.11.0, lote 3):
// barras con los niveles de banda de la referencia (lo que la corrección
// compara contra la sala), eje de frecuencias logarítmico y línea de 0 dB.
// Espejo de la vista "Referencia de espectro" del móvil (RoutingMapScreen).
//
// Componente pasivo: la consola lo refresca a 30 Hz vía setReference() (las
// bandas llegan ya copiadas bajo el lock del motor: no bloquea a la UI).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <mutex>

class RefSpectrum : public juce::Component {
public:
    RefSpectrum() {
        setSize(856, 96);
    }

    void setReference(bool captured, const std::vector<float>& levelsDb,
                      const std::vector<float>& frequencies) {
        std::lock_guard<std::mutex> lock(mutex_);
        captured_ = captured;
        levels_ = levelsDb;
        freqs_ = frequencies;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        bool captured = false;
        std::vector<float> levels, freqs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            captured = captured_;
            levels = levels_;
            freqs = freqs_;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour(theme::surfaceHi);
        g.fillRoundedRectangle(bounds, 6.0f);

        if (!captured || levels.empty()) {
            g.setColour(theme::textDim);
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("Sin referencia capturada: la corrección usa objetivo plano (0 dB en todas las bandas)."),
                       bounds.reduced(10.0f), juce::Justification::centred, false);
            return;
        }

        // Escala vertical: -40 dB (abajo) a +20 dB (arriba), con 0 dB marcada.
        const float lo = -40.0f, hi = 20.0f;
        const auto toY = [this, lo, hi](float db) {
            const float n = juce::jlimit(0.0f, 1.0f, (db - lo) / (hi - lo));
            return getLocalBounds().toFloat().getBottom() - 8.0f - n * (getHeight() - 24.0f);
        };
        const float y0 = toY(0.0f);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawLine(bounds.getX() + 8.0f, y0, bounds.getRight() - 8.0f, y0, 1.0f);

        // Barras por banda (color cian = la referencia; el móvil las muestra
        // como curva, aquí como barras para el lienzo pequeño).
        const int n = static_cast<int>(levels.size());
        const float w = bounds.getWidth() - 16.0f;
        const float bw = std::max(2.0f, w / std::max(1, n) - 2.0f);
        g.setColour(theme::primary.withAlpha(0.85f));
        for (int i = 0; i < n; ++i) {
            const float x = bounds.getX() + 8.0f + i * (w / n);
            const float y = toY(levels[i]);
            g.fillRect(juce::Rectangle<float>(x, y, bw, y0 - y));
        }

        // Etiquetas de frecuencia (las primeras y últimas 3 bandas, para no
        // amontonar texto en el lienzo de 96 px de alto).
        g.setFont(juce::Font(10.0f));
        g.setColour(theme::textDim);
        const int marks = std::min(n, 6);
        for (int i = 0; i < marks; ++i) {
            const int idx = n == 1 ? 0 : static_cast<int>((static_cast<double>(i) / (marks - 1) * (n - 1)));
            if (idx >= n) continue;
            const float x = bounds.getX() + 8.0f + idx * (w / n);
            juce::String f;
            const float hz = freqs.size() > static_cast<size_t>(idx) ? freqs[idx] : 0.0f;
            f = hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + juce::String::fromUTF8(" kHz")
                              : juce::String(hz, 0) + juce::String::fromUTF8(" Hz");
            g.drawText(f, juce::Rectangle<float>(x - 18.0f, bounds.getBottom() - 16.0f, 40.0f, 14.0f),
                       juce::Justification::centred, false);
        }
    }

private:
    mutable std::mutex mutex_;
    bool captured_ = false;
    std::vector<float> levels_;
    std::vector<float> freqs_;
};
