// SpatialJoystick.h — v1.11.0 — Espejo JUCE del SpatialJoystick.kt del móvil
// (la pantalla "Control"): un joystick circular para posicionar el elemento en
// el entorno virtual. Arrastrar dentro del círculo fija X (izquierda/derecha) e
// Y (frente/detrás), cada uno normalizado a -1..1 — los MISMOS valores que los
// atómicos/parámetros spatialX/spatialY de la app, el VST y el móvil.
//
// El dibujo replica al móvil: fondo radial, anillo exterior, anillos internos,
// crosshair, guías diagonales, marcas de dirección, etiquetas
// Frente/Atrás/Izq/Der, línea centro→thumb y el thumb con halo cian. El fondo
// puede pintar el entorno de trabajo (Sala/Estudio/Escenario/Exterior, las
// mismas 4 dimensiones que WorkEnvironmentType del móvil: solo cosmético, la
// ley de compensación no depende del entorno).
//
// Contrato de dos vías (igual que los sliders de Ajustes y las filas del
// editor del VST):
//   - mientras el usuario arrastra, el componente llama a onPosition(x, y)
//     (hilo de UI; el dueño escribe sus atómicos/parámetros y refresca su
//     lectura de compensación);
//   - un cambio EXTERNO (modo del catálogo, sync del móvil, undo/redo, recall
//     del DAW) llama a setPositionFromOutside(x, y); se ignora durante el
//     arrastre para no pelear con el ratón.
//
// El componente NO trae fuente de valores: el dueño decide si el dato pinta
// procede de atómicos (app) o de parámetros del APVTS (VSTs).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <cmath>

// Entornos de trabajo (espejo de WorkEnvironmentType del móvil): dimensiones
// en metros que solo afectan a la lectura ("Izq 1,2 m") y al dibujo del
// fondo — la compensación (core C) es la misma en los cuatro.
enum class SpatialEnvironment : int { Room = 0, Studio = 1, Stage = 2, Outdoor = 3 };

struct SpatialEnvironmentInfo {
    const char* label;
    float width;   // m (eje x)
    float depth;   // m (eje y)
    float height;  // m (0 en exterior)
    bool hasWalls;
};

inline SpatialEnvironmentInfo spatialEnvironmentInfo(SpatialEnvironment env) {
    switch (env) {
        case SpatialEnvironment::Room:    return {"Sala", 8.0f, 10.0f, 3.0f, true};
        case SpatialEnvironment::Studio:  return {"Estudio", 6.0f, 7.0f, 2.8f, true};
        case SpatialEnvironment::Stage:   return {"Escenario", 12.0f, 8.0f, 5.0f, true};
        case SpatialEnvironment::Outdoor: return {"Exterior", 20.0f, 30.0f, 0.0f, false};
    }
    return {"Sala", 8.0f, 10.0f, 3.0f, true};
}

// Lectura de posición (espejo de SpatialPosition.positionLabel del móvil):
// "Centro" cuando está centrado; si no, "Izq X,X m · Frente Y,Y m" con las
// distancias en metros según el ancho/fondo del entorno (solo se muestran los
// ejes que pasan de 0,3 m).
inline juce::String spatialPositionLabel(float x, float y, SpatialEnvironment env) {
    if (std::abs(x) < 0.05f && std::abs(y) < 0.05f)
        return juce::String::fromUTF8("Centro");
    const auto info = spatialEnvironmentInfo(env);
    const float metersX = x * info.width / 2.0f;
    const float metersY = y * info.depth / 2.0f;
    juce::String parts;
    if (metersX > 0.3f)
        parts << "Der " + juce::String(metersX, 1) + juce::String::fromUTF8(" m");
    else if (metersX < -0.3f)
        parts << juce::String::fromUTF8("Izq ") + juce::String(-metersX, 1) + juce::String::fromUTF8(" m");
    if (metersY > 0.3f) {
        if (parts.isNotEmpty()) parts << " · ";
        parts << juce::String::fromUTF8("Frente ") + juce::String(metersY, 1) + juce::String::fromUTF8(" m");
    } else if (metersY < -0.3f) {
        if (parts.isNotEmpty()) parts << " · ";
        parts << juce::String::fromUTF8("Atrás ") + juce::String(-metersY, 1) + juce::String::fromUTF8(" m");
    }
    return parts;
}

class SpatialJoystick : public juce::Component {
public:
    explicit SpatialJoystick(std::function<void(float, float)> onPosition)
        : onPosition_(std::move(onPosition)) {}

    // Actualiza el thumb desde fuera (modo/sync/undo/recall); se ignora
    // mientras el usuario arrastra.
    void setPositionFromOutside(float x, float y) {
        if (dragging_) return;
        if (x != x_ || y != y_) {
            x_ = juce::jlimit(-1.0f, 1.0f, x);
            y_ = juce::jlimit(-1.0f, 1.0f, y);
            repaint();
        }
    }

    float getX() const { return x_; }
    float getY() const { return y_; }
    bool isDragging() const { return dragging_; }
    void setEnvironment(SpatialEnvironment env) {
        if (env != environment_) { environment_ = env; repaint(); }
    }
    SpatialEnvironment getEnvironment() const { return environment_; }

    void mouseDown(const juce::MouseEvent& e) override { startDrag(e.position); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragging_) startDrag(e.position);
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (dragging_) { dragging_ = false; repaint(); }
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth(), h = bounds.getHeight();
        if (w < 8.0f || h < 8.0f) return;
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float radius = juce::jmin(w, h) * 0.5f;
        const float dragR = radius * 0.85f;   // como el móvil (0,85 del radio)
        // Elipse centrada en (cx, cy) de diámetro d.
        auto centred = [cx, cy](float d) {
            return juce::Rectangle<float>(cx - d * 0.5f, cy - d * 0.5f, d, d);
        };

        // === Fondo radial (el móvil usa gradiente radial de 3 paradas; aquí
        // se aproxima pintando elipses concéntricas interpoladas de afuera a
        // dentro: mismo aspecto) ===
        {
            const juce::Colour centre = theme::surfaceHi;
            const juce::Colour edge = theme::background;
            const int rings = 18;
            for (int i = rings - 1; i >= 0; --i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(rings);
                g.setColour(centre.interpolatedWith(edge, t));
                g.fillEllipse(centred(radius * t * 2.0f));
            }
        }

        // Entorno de fondo (solo si hay paredes): el "suelo" de la sala,
        // a escala de ancho×fondo, centrado y contenido en el círculo.
        if (spatialEnvironmentInfo(environment_).hasWalls) {
            const auto info = spatialEnvironmentInfo(environment_);
            const float scale = (radius * 1.5f) / juce::jmax(info.width, info.depth);
            const float rw = info.width * scale, rd = info.depth * scale;
            const auto floor = juce::Rectangle<float>(cx - rw * 0.5f, cy - rd * 0.5f, rw, rd);
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.fillRect(floor);
            g.setColour(juce::Colours::white.withAlpha(0.22f));
            g.drawRect(floor);
        }

        // === Anillo exterior (móvil: CyanDim 0,3, radio 0,92, 2 px) ===
        g.setColour(theme::primaryDark.withAlpha(0.9f));
        g.drawEllipse(centred(radius * 0.92f * 2.0f), 2.0f);

        // === Anillos internos 0,33 / 0,66 (blanco 0,06) ===
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawEllipse(centred(radius * 0.66f * 2.0f), 1.0f);
        g.drawEllipse(centred(radius * 0.33f * 2.0f), 1.0f);

        // === Crosshair (blanco 0,08; el móvil traza a 0,85 del radio) ===
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        const float cross = radius * 0.85f;
        g.drawLine(cx - cross, cy, cx + cross, cy);
        g.drawLine(cx, cy - cross, cx, cy + cross);

        // === Guías diagonales (blanco 0,04, longitud 0,6) ===
        g.setColour(juce::Colours::white.withAlpha(0.04f));
        const float diag = radius * 0.6f;
        for (int i = 0; i < 4; ++i) {
            const float a = (static_cast<float>(i) + 0.25f) * juce::MathConstants<float>::pi;
            g.drawLine(cx, cy, cx + std::cos(a) * diag, cy + std::sin(a) * diag);
        }

        // === Marcas de dirección (cian 0,5, 3 px, a 0,78–0,88 del radio) ===
        g.setColour(theme::primary.withAlpha(0.5f));
        const float r1 = radius * 0.78f, r2 = radius * 0.88f;
        g.drawLine(cx, cy - r2, cx, cy - r1, 3.0f);          // arriba
        g.drawLine(cx, cy + r1, cx, cy + r2, 3.0f);          // abajo
        g.drawLine(cx - r2, cy, cx - r1, cy, 3.0f);          // izq
        g.drawLine(cx + r1, cy, cx + r2, cy, 3.0f);          // der

        // === Etiquetas Frente/Atrás/Izq/Der (9 px, cian tenue) ===
        g.setFont(9.0f);
        g.setColour(theme::textDim);
        g.drawText(juce::String::fromUTF8("Frente"),
                   juce::Rectangle<float>(cx - radius, cy - radius + 2.0f, radius * 2.0f, 10.0f),
                   juce::Justification::centred);
        g.drawText(juce::String::fromUTF8("Atrás"),
                   juce::Rectangle<float>(cx - radius, cy + radius - 12.0f, radius * 2.0f, 10.0f),
                   juce::Justification::centred);
        g.drawText("Izq", juce::Rectangle<float>(cx - radius + 4.0f, cy - 4.0f, 18.0f, 10.0f),
                   juce::Justification::centredLeft);
        g.drawText("Der", juce::Rectangle<float>(cx + radius - 22.0f, cy - 4.0f, 18.0f, 10.0f),
                   juce::Justification::centredRight);

        // === Posición del thumb (el móvil invierte y: arriba = frente) ===
        const float tx = cx + x_ * dragR;
        const float ty = cy - y_ * dragR;

        // Halo (gradiente radial cian 0,4 → 0, radio ~0,36 del radio)
        {
            const float halo = juce::jmax(6.0f, radius * 0.36f);
            const int rings = 8;
            for (int i = rings - 1; i >= 0; --i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(rings);
                g.setColour(theme::primary.withAlpha(0.4f * (1.0f - t)));
                g.fillEllipse(centred(halo * t * 2.0f));
            }
        }

        // Línea centro→thumb (cian 0,4, 2 px)
        g.setColour(theme::primary.withAlpha(0.4f));
        g.drawLine(cx, cy, tx, ty, 2.0f);

        // Thumb: 3 círculos concéntricos en la posición (el móvil: 16/10/4 px
        // sobre 220; aquí se escalan al radio real)
        const float s = radius / 110.0f;
        auto atThumb = [tx, ty](float d) {
            return juce::Rectangle<float>(tx - d * 0.5f, ty - d * 0.5f, d, d);
        };
        g.setColour(theme::primary);
        g.fillEllipse(atThumb(32.0f * s));
        g.setColour(theme::background);
        g.fillEllipse(atThumb(20.0f * s));
        g.setColour(theme::primary);
        g.fillEllipse(atThumb(8.0f * s));
    }

private:
    void startDrag(juce::Point<float> p) {
        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float dragR = radius * 0.85f;
        const float dx = p.x - cx, dy = p.y - cy;
        const float dist = std::hypot(dx, dy);
        const float clamped = juce::jmin(dist, dragR);
        const float angle = (dist > 0.001f) ? std::atan2(dy, dx) : 0.0f;
        // Mismo mapeo que el móvil: x = cos, y = -sin (arriba del círculo =
        // "frente", y positivo).
        x_ = static_cast<float>(std::cos(angle) * clamped / dragR);
        y_ = static_cast<float>(-std::sin(angle) * clamped / dragR);
        dragging_ = true;
        if (onPosition_) onPosition_(x_, y_);
        repaint();
    }

    float x_ = 0.0f, y_ = 0.0f;
    bool dragging_ = false;
    SpatialEnvironment environment_ = SpatialEnvironment::Room;
    std::function<void(float, float)> onPosition_;
};
