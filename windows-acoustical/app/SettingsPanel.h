// SettingsPanel.h — Ajustes completos con layout real (cada fila posicionada,
// era la causa del panel vacío), tres modos rápidos (Salón / Cine / Estudio),
// sliders de alta precisión con caja de texto editable (teclado físico o el
// teclado en pantalla de Windows), hooks de deshacer y guardado automático,
// y un asistente con tutorial para todos los ajustes.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <AcousticalEngine.h>
#include "Theme.h"

class SettingsPanel : public juce::Component {
public:
    std::function<void()> onBeforeChange;   // guardar estado actual (deshacer)
    std::function<void()> onAfterChange;    // guardado automático

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
        if (auto* c = scroller_.getViewedComponent())
            c->setBounds(0, 0, 660, contentHeight_);
    }

    void refresh() { rebuild(); }

private:
    // Cambio de configuración con deshacer y guardado automático integrados
    template <typename F>
    void mutate(F f) {
        if (onBeforeChange) onBeforeChange();
        auto c = engine_.config();
        f(c);
        engine_.configure(c);
        if (onAfterChange) onAfterChange();
    }

    // === Construcción del panel ===

    juce::Component& content() { return *scroller_.getViewedComponent(); }

    void addGroup(const juce::String& title) {
        endGroupIfOpen();
        auto* box = new juce::GroupComponent(title, title);
        boxes_.add(box);
        content().addAndMakeVisible(box);
        groupStartY_ = y_;
        groupOpen_ = true;
        y_ += 26;
    }

    void endGroupIfOpen() {
        if (!groupOpen_) return;
        boxes_.getLast()->setBounds(4, groupStartY_ - 2, 652, y_ - groupStartY_ + 14);
        groupOpen_ = false;
        y_ += 16;
    }

    juce::Slider* addSlider(const juce::String& label, double minV, double maxV, double step,
                            double value, std::function<void(double)> onChange,
                            double skew = 1.0, int decimals = 2) {
        auto* s = new juce::Slider(juce::Slider::LinearHorizontal,
                                   juce::Slider::TextBoxRight);
        auto range = juce::NormalisableRange<double>(minV, maxV, step);
        if (skew != 1.0) range.skew = skew;  // p. ej. 0.25 = escala logarítmica
        s->setNormalisableRange(range);
        s->setValue(value, juce::dontSendNotification);
        s->setNumDecimalPlacesToDisplay(decimals);
        // Caja editable: escribe el valor exacto con el teclado
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 22);
        s->onValueChange = [s, onChange] { onChange(s->getValue()); };
        widgets_.add(s);
        addControlRow(label, s);
        return s;
    }

    juce::ToggleButton* addToggle(const juce::String& label, bool value,
                                  std::function<void(bool)> onChange) {
        auto* t = new juce::ToggleButton(label);
        t->setToggleState(value, juce::dontSendNotification);
        t->onStateChange = [t, onChange] { onChange(t->getToggleState()); };
        t->setBounds(24, y_, 590, 24);
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
        addControlRow(label, c);
        return c;
    }

    juce::TextButton* addButton(const juce::String& text, std::function<void()> onClick) {
        auto* b = new juce::TextButton(text);
        b->onClick = std::move(onClick);
        widgets_.add(b);
        content().addAndMakeVisible(b);
        return b;
    }

    void addControlRow(const juce::String& label, juce::Component* ctl) {
        auto* lab = new juce::Label({}, label);
        lab->setColour(juce::Label::textColourId, theme::textDim);
        rowLabels_.add(lab);
        content().addAndMakeVisible(lab);
        lab->setBounds(16, y_, 218, 22);
        ctl->setBounds(242, y_ - 2, 404, 26);
    }

    void endRow() { y_ += 34; }

    // === Panel ===

    void rebuild() {
        widgets_.clear(false);
        rowLabels_.clear(false);
        boxes_.clear(false);
        groupOpen_ = false;
        genToggle_ = nullptr;
        y_ = 8;
        content().removeAllChildren();

        const auto& cfg = engine_.config();

        addModoRow();
        addAyudaRow();

        // --- Motor de audio ---
        addGroup("Motor de audio");
        addCombo("Muestreo", {"44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"},
                 static_cast<int>(cfg.sampleRate), [this](int i) {
                     mutate([i](auto& c) {
                         c.sampleRate = static_cast<acoustical::SampleRate>(i);
                     });
                 });
        endRow();
        addCombo("FFT", {"512", "1K", "2K", "4K", "8K"},
                 static_cast<int>(cfg.fftSize), [this](int i) {
                     mutate([i](auto& c) {
                         c.fftSize = static_cast<acoustical::FftSize>(i);
                     });
                 });
        endRow();
        addCombo("Intervalo de análisis", {"25 ms", "50 ms", "100 ms", "200 ms", "500 ms"},
                 static_cast<int>(cfg.analysisInterval), [this](int i) {
                     mutate([i](auto& c) {
                         c.analysisInterval = static_cast<acoustical::AnalysisInterval>(i);
                     });
                 });
        endRow();
        addCombo("Bandas", {"8", "10", "16", "31 · 1/3 oct", "124 · Ultra"},
                 static_cast<int>(cfg.bandCount), [this](int i) {
                     mutate([i](auto& c) {
                         c.bandCount = static_cast<acoustical::BandCount>(i);
                     });
                 });
        endRow();
        addSlider("Ganancia máxima", 1, 50, 0.5, cfg.maxGainDb, [this](double v) {
            mutate([v](auto& c) { c.maxGainDb = static_cast<float>(v); });
        });
        endRow();
        addSlider("Suavizado", 0.05, 0.8, 0.01, cfg.smoothingFactor, [this](double v) {
            mutate([v](auto& c) { c.smoothingFactor = static_cast<float>(v); });
        }, 1.0, 3);
        endRow();
        addSlider("Umbral de ruido", -140, -60, 0.5, cfg.noiseFloorDb, [this](double v) {
            mutate([v](auto& c) { c.noiseFloorDb = static_cast<float>(v); });
        }, 1.0, 1);
        endRow();
        addToggle("Corrección activada", cfg.correctionEnabled, [this](bool on) {
            mutate([on](auto& c) { c.correctionEnabled = on; });
        });
        endRow();
        addToggle("Sustracción de ruido", cfg.noiseSubtractionEnabled, [this](bool on) {
            mutate([on](auto& c) { c.noiseSubtractionEnabled = on; });
        });
        endRow();
        addToggle("Canales L/R enlazados (Link)", true, [this](bool on) {
            engine_.setEqChannelLinked(on);
        });
        endRow();

        // --- Un grupo por cada ecu dinámico (los tres son el mismo corrector) ---
        for (int i = 0; i < 3; ++i)
            addDynamicEqGroup(i);

        // --- Trabajo ---
        addGroup("Trabajo");
        addSlider("Retardo global", 0, 100, 0.01, cfg.audioDelayMs, [this](double v) {
            mutate([v](auto& c) { c.audioDelayMs = static_cast<float>(v); });
        }, 1.0, 2);
        endRow();
        addSlider("SPL objetivo", 40, 110, 0.1, cfg.targetSpl, [this](double v) {
            mutate([v](auto& c) { c.targetSpl = static_cast<float>(v); });
        }, 1.0, 1);
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
        endRow();

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
        }, 0.25, 1);
        endRow();
        addSlider("Nivel", -40, 0, 0.5, -12, [this](double v) {
            engine_.signalGenerator().setLevelDb(static_cast<float>(v));
        }, 1.0, 1);
        endRow();
        addToggle("Generador activo (sale por el EQ)", false,
                  [this](bool on) { if (onGeneratorActive_) onGeneratorActive_(on); });
        endRow();

        endGroupIfOpen();
        contentHeight_ = y_ + 16;
        resized();
    }

    // === Modos rápidos ===

    void addModoRow() {
        addGroup("Modos rápidos — toca, escucha y afina después a tu gusto");
        const char* names[3] = {"Salón (música)", "Cine (graves)", "Estudio (precisión)"};
        for (int m = 0; m < 3; ++m) {
            auto* b = addButton(names[m], [this, m] { applyMode(m); });
            b->setBounds(16 + m * 214, y_, 202, 32);
        }
        endRow();
    }

    void applyMode(int m) {
        struct Modo {
            int bandCount, analysisInterval, fftSize;
            float maxGainDb, smoothingFactor, targetSpl;
            bool noiseSubtraction;
        };
        static const Modo modos[3] = {
            {3, 2, 1, 12.0f, 0.30f, 75.0f, true},   // Salón: 31 bandas, 100 ms
            {3, 3, 2, 18.0f, 0.50f, 85.0f, true},   // Cine: 31 bandas, 200 ms
            {3, 1, 4, 6.0f,  0.20f, 70.0f, false},  // Estudio: 31 bandas, 50 ms, FFT 8K
        };
        const auto& modo = modos[m];
        mutate([modo](auto& c) {
            c.bandCount = static_cast<acoustical::BandCount>(modo.bandCount);
            c.analysisInterval = static_cast<acoustical::AnalysisInterval>(modo.analysisInterval);
            c.fftSize = static_cast<acoustical::FftSize>(modo.fftSize);
            c.maxGainDb = modo.maxGainDb;
            c.smoothingFactor = modo.smoothingFactor;
            c.targetSpl = modo.targetSpl;
            c.noiseSubtractionEnabled = modo.noiseSubtraction;
            c.correctionEnabled = true;
        });
        rebuild();  // los deslizadores muestran los valores nuevos
    }

    // === Ayuda / asistente ===

    void addAyudaRow() {
        addGroup("Ayuda");
        auto* help = addButton("Asistente y tutorial", [this] { showHelp(); });
        help->setBounds(16, y_, 200, 32);
        auto* kb = addButton("Teclado en pantalla", [] {
            juce::Process::openDocument("osk.exe", {});
        });
        kb->setBounds(230, y_, 200, 32);
        endRow();
    }

    void showHelp() {
        auto* ed = new juce::TextEditor("ayuda");
        ed->setMultiLine(true, true);
        ed->setReadOnly(true);
        ed->setFont(juce::Font(14.0f));
        ed->setColour(juce::TextEditor::backgroundColourId, theme::surface);
        ed->setColour(juce::TextEditor::textColourId, juce::Colours::whitesmoke);
        ed->setColour(juce::TextEditor::outlineColourId, theme::surfaceHi);
        ed->setText(helpText(), false);
        ed->setCaretVisible(false);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(ed);
        o.content->setSize(680, 580);
        o.dialogTitle = "Asistente y tutorial de Acoustical Estudio";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = theme::background;
        o.launchAsync();
    }

    static juce::String helpText() {
        return juce::String(
            "PASO A PASO RECOMENDADO\n"
            "1. Pestaña Audio: elige tu tarjeta de entrada y salida (altavoces, HDMI,\n"
            "    Bluetooth, USB o interface). Ahí también van la frecuencia de muestreo\n"
            "    y el tamaño de buffer.\n"
            "2. Pulsa Capturar ruido (barra superior) en silencio: aprende el fondo de\n"
            "    tu sala y activa la sustracción de ruido.\n"
            "3. Dale a Capturar referencia: el motor toma el nivel de cada banda y\n"
            "    empieza a corregir solo.\n"
            "4. Los tres ecuas dinámicos trabajan solos: cian (donde más se necesita),\n"
            "    ámbar (graves) y magenta (agudos). Actívalos con F1, F2 y F3.\n\n"
            "MODOS RÁPIDOS\n"
            "Salón: corrección musical equilibrada (31 bandas, 100 ms).\n"
            "Cine: deja subir más los graves y reacciona más lento (18 dB, 200 ms).\n"
            "Estudio: máxima fidelidad, poca intervención (6 dB, 50 ms, FFT 8K).\n"
            "Después del modo, TODO es editable: los cambios se guardan solos.\n\n"
            "VALORES EXACTOS\n"
            "Cada deslizador tiene su caja de texto a la derecha: haz clic y escribe el\n"
            "valor exacto (físico o con Teclado en pantalla). Precisión: retardos de\n"
            "0,01 ms, suavizado con 3 decimales, SPL con décimas.\n\n"
            "DESHACER\n"
            "Ctrl+Z deshace cualquier cambio de ajustes, Ctrl+Shift+Z o Ctrl+Y lo rehace.\n"
            "Bloquear faders evita mover el EQ por un toque accidental.\n\n"
            "AJUSTES PRINCIPALES\n"
            "Bandas: resolución del corrector (8/10/16/31/124 bandas).\n"
            "Intervalo de análisis: cada cuánto se mide la sala (25-500 ms).\n"
            "Suavizado: cuánto se fía del último análisis (0,05 nervioso - 0,8 estable).\n"
            "Umbral de ruido: por debajo de este nivel no se corrige.\n"
            "Retardo global: alinea el sonido con la imagen (pasos de 0,01 ms).\n\n"
            "GENERADOR DE SEÑALES\n"
            "Seno por banda y barrido log sirven para verificar cada banda a mano;\n"
            "ruido rosa para probar el sistema completo. Sale a través del EQ.\n\n"
            "MÓVIL POR USB\n"
            "Conecta el móvil: se sincronizan perfiles y ajustes en las dos direcciones.\n"
            "Las actualizaciones de Windows llegan por el propio cable (con tu permiso).");
    }

    // === Ecuas dinámicos ===

    void addDynamicEqGroup(int index) {
        const juce::String n = juce::String(index + 1);
        auto& sweeper = engine_.sweeperFor(index);  // acceso directo a parámetros en vivo

        addGroup("Ecu dinámico " + n);
        addToggle("Activado", index == 0, [this, index](bool on) {
            engine_.setDynamicEqEnabled(index, on);
        });
        endRow();
        addSlider("Intervalo de decisión (ms)",
                  acoustical::DynamicEqConfig::MIN_INTERVAL_MS,
                  acoustical::DynamicEqConfig::MAX_INTERVAL_MS, 50,
                  sweeper.decisionIntervalMs.load(), [this, index](double v) {
                      engine_.setDynamicEqInterval(index, static_cast<int>(v));
                  });
        endRow();
        addSlider("Ganancia máxima (dB)", 1, 50, 0.5, sweeper.maxGainDb.load(),
                  [this, index](double v) {
                      engine_.setDynamicEqMaxGain(index, static_cast<float>(v));
                  }, 1.0, 1);
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
                      }, 0.25, 1);
            endRow();
            addSlider("Apoyo " + juce::String(b + 1) + " · ganancia (dB)", -12, 12, 0.25, 0,
                      [this, index, b](double v) {
                          supportGain_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      }, 1.0, 2);
            endRow();
        }
    }

    void pushSupportBands(int index) {
        std::vector<acoustical::SupportBand> bands;
        for (int b = 0; b < 4; ++b)
            bands.push_back({supportFreq_[index][b], supportGain_[index][b], 2.0f});
        engine_.setSupportBands(index, bands);
    }

    int y_ = 8;
    int contentHeight_ = 400;
    int groupStartY_ = 0;
    bool groupOpen_ = false;

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
