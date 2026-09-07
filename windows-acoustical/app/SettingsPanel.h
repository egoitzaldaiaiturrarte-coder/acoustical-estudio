// SettingsPanel.h — menú de Ajustes completo: motor, sección específica por
// cada ecu dinámico (intervalo, ganancia, mezclador, velocidad, barridos
// extra, bandas de apoyo), trabajo (L/R, retardo 0,01 ms, verificación
// automática, entorno, SPL, posición espacial) y generador de señales.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <AcousticalEngine.h>
#include "Theme.h"

class SettingsPanel : public juce::Component {
public:
    SettingsPanel(acoustical::AcousticalEngine& engine,
                  std::function<void(bool)> onGeneratorActive)
        : engine_(engine), onGeneratorActive_(std::move(onGeneratorActive)) {
        scroller_.setViewedComponent(new juce::Component(), true);
        addAndMakeVisible(scroller_);
        rebuild();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        scroller_.setBounds(getLocalBounds());
        if (auto* content = scroller_.getViewedComponent())
            content->setBounds(0, 0, getWidth() - 12, contentHeight_);
    }

    void refresh() { rebuild(); }

private:
    // === Helpers de construcción ===

    juce::Component& content() { return *scroller_.getViewedComponent(); }

    void addGroup(const juce::String& title) {
        auto& c = content();
        auto* box = new juce::GroupComponent(title, title);
        boxes_.add(box);
        c.addAndMakeVisible(box);
        currentGroupBounds_ = juce::Rectangle<int>(12, y_, 600, 0);
        groupBounds_ = &boxes_.getLast()->getBounds();
    }

    juce::Slider* addSlider(const juce::String& label, double minV, double maxV, double step,
                            double value, std::function<void(double)> onChange,
                            double skew = 1.0) {
        auto* s = new juce::Slider(juce::Slider::LinearHorizontal,
                                   juce::Slider::TextBoxRight);
        auto range = juce::NormalisableRange<double>(minV, maxV, step);
        if (skew != 1.0) range.setSkewForCentre((minV + maxV) / 2.0, skew);
        s->setNormalisableRange(range);
        s->setValue(value, juce::dontSendNotification);
        s->onValueChange = [s, onChange] { onChange(s->getValue()); };
        widgets_.add(s);
        content().addAndMakeVisible(s);
        rowLabels_.add(new juce::Label({}, label));
        rowLabels_.getLast()->setColour(juce::Label::textColourId, theme::textDim);
        content().addAndMakeVisible(rowLabels_.getLast());
        return s;
    }

    juce::ToggleButton* addToggle(const juce::String& label, bool value,
                                  std::function<void(bool)> onChange) {
        auto* t = new juce::ToggleButton(label);
        t->setToggleState(value, juce::dontSendNotification);
        t->onStateChange = [t, onChange] { onChange(t->getToggleState()); };
        widgets_.add(t);
        content().addAndMakeVisible(t);
        return t;
    }

    juce::ComboBox* addCombo(const juce::String& label,
                             const juce::StringArray& options, int selected,
                             std::function<void(int)> onChange) {
        auto* c = new juce::ComboBox();
        for (int i = 0; i < options.size(); ++i) c->addItem(options[i], i + 1);
        c->setSelectedItemIndex(selected, juce::dontSendNotification);
        c->onChange = [c, onChange] { onChange(c->getSelectedItemIndex()); };
        widgets_.add(c);
        content().addAndMakeVisible(c);
        rowLabels_.add(new juce::Label({}, label));
        rowLabels_.getLast()->setColour(juce::Label::textColourId, theme::textDim);
        content().addAndMakeVisible(rowLabels_.getLast());
        return c;
    }

    void endRow() { y_ += 34; }

    // === Panel ===

    void rebuild() {
        widgets_.clear(false);
        rowLabels_.clear(false);
        boxes_.clear(false);
        y_ = 8;
        content().removeAllChildren();

        const auto& cfg = engine_.config();

        // --- Motor de audio ---
        addGroup("Motor de audio");
        addCombo("Muestreo", {"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"},
                 static_cast<int>(cfg.sampleRate), [this](int i) {
                     auto c = engine_.config();
                     c.sampleRate = static_cast<acoustical::SampleRate>(i);
                     engine_.configure(c);
                 });
        endRow();
        addCombo("FFT", {"512", "1K", "2K", "4K", "8K"},
                 static_cast<int>(cfg.fftSize), [this](int i) {
                     auto c = engine_.config();
                     c.fftSize = static_cast<acoustical::FftSize>(i);
                     engine_.configure(c);
                 });
        endRow();
        addCombo("Intervalo de análisis", {"25 ms", "50 ms", "100 ms", "200 ms", "500 ms"},
                 static_cast<int>(cfg.analysisInterval), [this](int i) {
                     auto c = engine_.config();
                     c.analysisInterval = static_cast<acoustical::AnalysisInterval>(i);
                     engine_.configure(c);
                 });
        endRow();
        addCombo("Bandas", {"8", "10", "16", "31 · 1/3 oct", "124 · Ultra"},
                 static_cast<int>(cfg.bandCount), [this](int i) {
                     auto c = engine_.config();
                     c.bandCount = static_cast<acoustical::BandCount>(i);
                     engine_.configure(c);
                 });
        endRow();
        addSlider("Ganancia máxima", 1, 50, 1, cfg.maxGainDb, [this](double v) {
            auto c = engine_.config();
            c.maxGainDb = static_cast<float>(v);
            engine_.configure(c);
        });
        endRow();
        addSlider("Suavizado", 0.05, 0.8, 0.01, cfg.smoothingFactor, [this](double v) {
            auto c = engine_.config();
            c.smoothingFactor = static_cast<float>(v);
            engine_.configure(c);
        });
        endRow();
        addSlider("Umbral de ruido", -140, -60, 1, cfg.noiseFloorDb, [this](double v) {
            auto c = engine_.config();
            c.noiseFloorDb = static_cast<float>(v);
            engine_.configure(c);
        });
        endRow();
        addToggle("Corrección activada", cfg.correctionEnabled, [this](bool on) {
            auto c = engine_.config();
            c.correctionEnabled = on;
            engine_.configure(c);
        });
        addToggle("Sustracción de ruido", cfg.noiseSubtractionEnabled, [this](bool on) {
            auto c = engine_.config();
            c.noiseSubtractionEnabled = on;
            engine_.configure(c);
        });
        y_ += 34;
        addToggle("Canales L/R enlazados (Link)", true, [this](bool on) {
            engine_.setEqChannelLinked(on);
        });
        y_ += 40;

        // --- Un grupo por cada ecu dinámico (los tres son el mismo corrector) ---
        for (int i = 0; i < 3; ++i)
            addDynamicEqGroup(i);

        // --- Trabajo ---
        addGroup("Trabajo");
        addSlider("Retardo global", 0, 100, 0.01, cfg.audioDelayMs, [this](double v) {
            auto c = engine_.config();
            c.audioDelayMs = static_cast<float>(v);   // pasos de 0,01 ms
            engine_.configure(c);
        });
        endRow();
        addSlider("SPL objetivo", 40, 110, 1, cfg.targetSpl, [this](double v) {
            auto c = engine_.config();
            c.targetSpl = static_cast<float>(v);
            engine_.configure(c);
        });
        endRow();
        addSlider("Posición X (izq/der)", -1, 1, 0.01, 0, [this](double) { /* SPL */ });
        endRow();
        addSlider("Posición Y (atrás/frente)", -1, 1, 0.01, 0, [this](double) { /* SPL */ });
        endRow();
        addSlider("Distancia", 0, 1, 0.01, 0.5, [this](double) { /* SPL */ });
        endRow();
        addSlider("Tamaño del elemento", 0, 1, 0.01, 0.5, [this](double) { /* SPL */ });
        endRow();
        addCombo("Ciclo de verificación", {"15 s", "30 s", "60 s", "120 s"}, 2,
                 [](int) { /* verificación automática EQ3 */ });
        endRow();
        addCombo("Calidad de la verificación", {"Baja", "Normal", "Alta"}, 1, [](int) {});
        endRow();
        addCombo("Profundidad de bits", {"16 bits", "24 bits"}, 1, [](int) {});
        y_ += 40;

        // --- Generador de señales ---
        addGroup("Generador de señales");
        addCombo("Señal", {"Silencio", "Seno", "Seno por banda", "Barrido log",
                           "Ruido rosa", "Ruido blanco"}, 0, [this](int i) {
            auto& gen = engine_.signalGenerator();
            gen.setWaveform(static_cast<acoustical::SignalGenerator::Waveform>(
                i == 0 ? acoustical::SignalGenerator::Waveform::Silence
              : i == 1 ? acoustical::SignalGenerator::Waveform::Sine
              : i == 2 ? acoustical::SignalGenerator::Waveform::BandSine
              : i == 3 ? acoustical::SignalGenerator::Waveform::LogSweep
              : i == 4 ? acoustical::SignalGenerator::Waveform::PinkNoise
                       : acoustical::SignalGenerator::Waveform::WhiteNoise));
        });
        endRow();
        addSlider("Frecuencia", 20, 20000, 1, 1000, [this](double v) {
            engine_.signalGenerator().setFrequency(static_cast<float>(v));
        }, 0.25);  // escala logarítmica
        endRow();
        addSlider("Nivel", -40, 0, 1, -12, [this](double v) {
            engine_.signalGenerator().setLevelDb(static_cast<float>(v));
        });
        endRow();
        genToggle_ = addToggle("Generador activo (sale por el EQ)", false,
                               [this](bool on) { if (onGeneratorActive_) onGeneratorActive_(on); });
        y_ += 44;

        contentHeight_ = y_ + 20;
        resized();
    }

    void addDynamicEqGroup(int index) {
        const juce::String n = juce::String(index + 1);
        auto& sweeper = engine_.sweeperFor(index);  // acceso directo a parámetros en vivo

        addGroup("Ecu dinámico " + n);
        addToggle("Activado", index == 0, [this, index](bool on) {
            engine_.setDynamicEqEnabled(index, on);
        });
        y_ += 34;
        addSlider("Intervalo de decisión (ms)",
                  acoustical::DynamicEqConfig::MIN_INTERVAL_MS,
                  acoustical::DynamicEqConfig::MAX_INTERVAL_MS, 50,
                  sweeper.decisionIntervalMs.load(), [this, index](double v) {
                      engine_.setDynamicEqInterval(index, static_cast<int>(v));
                  });
        endRow();
        addSlider("Ganancia máxima (dB)", 1, 50, 1, sweeper.maxGainDb.load(),
                  [this, index](double v) {
                      engine_.setDynamicEqMaxGain(index, static_cast<float>(v));
                  });
        endRow();
        addSlider("Mezclador propio", 0, 1, 0.01, sweeper.mixerLevel.load(),
                  [this, index](double v) {
                      engine_.setDynamicEqMixerLevel(index, static_cast<float>(v));
                  });
        endRow();
        addCombo("Velocidad del suavizado", {"×0.5", "×1", "×2", "×4"}, 1,
                 [this, index](int i) {
                     static const float speeds[4] = {0.5f, 1.0f, 2.0f, 4.0f};
                     engine_.setDynamicEqSpeed(index, speeds[i]);
                 });
        endRow();
        addCombo("Barridos extra", {"0", "1", "2", "3", "4", "5", "6"},
                 sweeper.extraSweeps.load(), [this, index](int i) {
                     engine_.setDynamicEqExtras(index, i);
                 });
        endRow();

        // Bandas de apoyo de frecuencia libre (4 por ecu, igual que en el móvil)
        for (int b = 0; b < 4; ++b) {
            addSlider("Apoyo " + juce::String(b + 1) + " · frecuencia", 20, 20000, 1,
                      250.0 * std::pow(4.0, b), [this, index, b](double v) {
                          supportFreq_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      }, 0.25);
            endRow();
            addSlider("Apoyo " + juce::String(b + 1) + " · ganancia (dB)", -12, 12, 0.5, 0,
                      [this, index, b](double v) {
                          supportGain_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      });
            endRow();
        }
        y_ += 36;
    }

    void pushSupportBands(int index) {
        std::vector<acoustical::SupportBand> bands;
        for (int b = 0; b < 4; ++b)
            bands.push_back({supportFreq_[index][b], supportGain_[index][b], 2.0f});
        engine_.setSupportBands(index, bands);
    }

    int y_ = 8;
    int contentHeight_ = 400;
    juce::Rectangle<int>* groupBounds_ = nullptr;
    juce::Rectangle<int> currentGroupBounds_;

    juce::Viewport scroller_;
    juce::OwnedArray<juce::Component> widgets_;
    juce::OwnedArray<juce::Label> rowLabels_;
    juce::OwnedArray<juce::GroupComponent> boxes_;
    juce::ToggleButton* genToggle_ = nullptr;

    float supportFreq_[3][4] = {{250, 1000, 4000, 12000}, {250, 1000, 4000, 12000},
                                {250, 1000, 4000, 12000}};
    float supportGain_[3][4] = {};

    acoustical::AcousticalEngine& engine_;
    std::function<void(bool)> onGeneratorActive_;
};
