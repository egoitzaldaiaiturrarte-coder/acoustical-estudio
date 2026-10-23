// SplGauge.h — medidor circular de SPL (v1.11.0, lote 3): port de
// SplGauge.kt del móvil. Arco de 270° (con el hueco de 90° por debajo)
// cubriendo 30–120 dB, zonas codificadas por color según el nivel, marcador
// del objetivo, marcas en 30/50/70/90/110, lectura central y la pastilla
// "Objetivo alcanzado" mientras el nivel está en o sobre el objetivo (y el
// motor corriendo).
//
// Es un componente pasivo: la consola le manda el valor a 30 Hz vía
// setValues() (el motor entrega el SPL en su análisis y la UI no quiere
// bloquearse en un lock del motor para leerlo).
//
// Convención de ángulos de esta JUCE: en radianes, 0 = arriba (12 h) y
// sentido horario; el arco va de 225° (1.25·pi, bajo-izquierda) a 495°
// (2.75·pi, bajo-derecha), pasando por arriba.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <mutex>

class SplGauge : public juce::Component {
public:
    SplGauge() {
        setSize(240, 240);
    }

    // (Hilo de mensajes) valor actual, objetivo y si el motor está en
    // marcha: solo escribe bajo lock propio y pide repaint.
    void setValues(float spl, float targetSpl, bool engineRunning) {
        std::lock_guard<std::mutex> lock(mutex_);
        spl_ = spl;
        targetSpl_ = targetSpl;
        reached_ = engineRunning && spl > 0.0f && spl >= targetSpl_;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        float spl = 0.0f, target = 75.0f;
        bool reached = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            spl = spl_;
            target = targetSpl_;
            reached = reached_;
        }

        auto bounds = getLocalBounds().toFloat();
        const float side = std::min(bounds.getWidth(), bounds.getHeight() - 36.0f);
        auto gauge = juce::Rectangle<float>(
            (bounds.getWidth() - side) / 2.0f, 0.0f, side, side);
        const juce::Point<float> center = gauge.getCentre();
        const float radius = side / 2.0f;
        const float stroke = radius * 0.12f;
        const auto arcRect = gauge.reduced(stroke / 2.0f);

        const float minVal = 30.0f, maxVal = 120.0f;
        // Ángulo (radianes, 0 = arriba, horario) de un valor dB: 225° +
        // 270° × normalización.
        const auto toAngle = [minVal, maxVal](float v) {
            const float n = juce::jlimit(0.0f, 1.0f, (v - minVal) / (maxVal - minVal));
            return 1.25f * juce::MathConstants<float>::pi + n * 1.5f * juce::MathConstants<float>::pi;
        };
        // Punto sobre la circunferencia (a = radianes desde arriba, horario).
        const auto polar = [&center](float a, float r) {
            return juce::Point<float>(center.x + r * std::sin(a), center.y - r * std::cos(a));
        };
        auto makeArc = [&arcRect](float from, float to) {
            juce::Path p;
            p.addArc(arcRect.getX(), arcRect.getY(), arcRect.getWidth(), arcRect.getHeight(),
                     from, to, true);
            return p;
        };

        // Arco de fondo (las 270° completas).
        g.setColour(theme::surfaceHi);
        g.strokePath(makeArc(1.25f * juce::MathConstants<float>::pi,
                              2.75f * juce::MathConstants<float>::pi),
                     juce::PathStrokeType(stroke));

        // Arco activo: color por zona de nivel (como el móvil: <40 cian,
        // <70 verde, <85 ámbar, más coral).
        if (spl > 0.0f) {
            const juce::Colour arc = spl < 40.0f ? theme::primary
                                : spl < 70.0f ? theme::positive
                                : spl < 85.0f ? theme::eq2Amber
                                               : theme::danger;
            g.setColour(arc);
            g.strokePath(makeArc(1.25f * juce::MathConstants<float>::pi, toAngle(spl)),
                         juce::PathStrokeType(stroke));
        }

        // Marcas en 30/50/70/90/110.
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        for (float db : {30.0f, 50.0f, 70.0f, 90.0f, 110.0f}) {
            const float a = toAngle(db);
            const auto pIn = polar(a, radius - stroke * 1.3f);
            const auto pOut = polar(a, radius - stroke * 1.15f);
            g.drawLine(pIn.x, pIn.y, pOut.x, pOut.y, 2.0f);
        }

        // Marcador del objetivo: línea que cruza el arco en el ángulo del
        // objetivo (cian, como el móvil).
        {
            const float a = toAngle(target);
            const auto pIn = polar(a, radius - stroke / 2.0f - stroke * 0.6f);
            const auto pOut = polar(a, radius - stroke / 2.0f + stroke * 0.6f);
            g.setColour(theme::primary);
            g.drawLine(pIn.x, pIn.y, pOut.x, pOut.y, 4.0f);
        }

        // Lectura central: valor grande + "dB SPL" + objetivo.
        const juce::String value = spl > 0.0f ? juce::String(spl, 1) : juce::String("--");
        g.setFont(juce::Font(42.0f, juce::Font::plain));
        g.setColour(theme::textPrimary);
        g.drawText(value, gauge.removeFromBottom(52.0f).withX(gauge.getX() + 6.0f),
                   juce::Justification::centredRight, false);
        g.setFont(juce::Font(12.0f));
        g.setColour(theme::textDim);
        g.drawText(juce::String::fromUTF8("dB SPL"),
                   gauge.removeFromBottom(18.0f), juce::Justification::centredLeft, false);
        g.setColour(theme::primary);
        g.drawText(juce::String::fromUTF8("Objetivo: ") + juce::String(target, 0),
                   gauge.removeFromBottom(18.0f), juce::Justification::centredLeft, false);

        // Pastilla "Objetivo alcanzado" (solo mientras se cumple, como el
        // móvil): fondo de la zona con la que se colorea, punto + texto.
        auto pill = juce::Rectangle<float>(
            bounds.getX() + (bounds.getWidth() - 200.0f) / 2.0f,
            side + 8.0f, 200.0f, 24.0f);
        if (reached) {
            g.setColour(theme::positive.withAlpha(0.15f));
            g.fillRoundedRectangle(pill, 6.0f);
            g.setColour(theme::positive);
            g.fillRoundedRectangle(pill.removeFromLeft(28.0f).withX(pill.getX() + 8.0f)
                                       .reduced(4.0f), 3.0f);
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("Objetivo alcanzado"),
                       pill.withX(pill.getX() + 24.0f), juce::Justification::centredLeft, false);
        } else if (spl > 0.0f) {
            // Fuera de objetivo: pista discreta de cuánto falta (el móvil
            // solo muestra la pastilla verde; aquí se ve el estado siempre).
            g.setColour(theme::textDim.withAlpha(0.6f));
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("a ") + juce::String(target - spl, 1)
                       + juce::String::fromUTF8(" dB del objetivo"), pill,
                       juce::Justification::centred, false);
        }
    }

private:
    mutable std::mutex mutex_;
    float spl_ = 0.0f;
    float targetSpl_ = 75.0f;
    bool reached_ = false;
};
