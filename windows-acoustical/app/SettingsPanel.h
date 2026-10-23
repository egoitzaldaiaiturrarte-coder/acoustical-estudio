// SettingsPanel.h — Ajustes completos con layout real (cada fila posicionada,
// era la causa del panel vacío), tres modos rápidos (Salón / Cine / Estudio),
// sliders de alta precisión con caja de texto editable (teclado físico o el
// teclado en pantalla de Windows), hooks de deshacer y guardado automático,
// y un asistente con tutorial para todos los ajustes.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <AcousticalEngine.h>
#include "PhoneLink.h"
#include "Theme.h"
#include "SpatialJoystick.h"
#include "SplGauge.h"
#include "RefSpectrum.h"

class SettingsPanel : public juce::Component {
public:
    std::function<void()> onBeforeChange;   // guardar estado actual (deshacer)
    std::function<void()> onAfterChange;    // guardado automático
    std::function<void()> onReceiveFromPhone;  // "Recibir del móvil" (Ajustes del móvil)

    // Los referenciados de la posición espacial (atómicos de la consola: la UI
    // escribe, el callback de audio lee) y los callbacks de modos:
    //  - onApplyMode: aplicar un perfil del catálogo (lo llama el grupo
    //    "Modos de trabajo"; la consola configura motor + ecuas + espacio +
    //    generador y persiste el modo activo).
    //  - activeModeId: el id del modo activo (para resaltar el botón en el
    //    rebuild) — "" = ajustes personalizados (no un modo del catálogo).
    SettingsPanel(acoustical::AcousticalEngine& engine, PhoneLink* phone,
                  std::function<void(bool)> onGeneratorActive,
                  std::atomic<float>& spatialX, std::atomic<float>& spatialY,
                  std::atomic<float>& spatialZ, std::atomic<float>& spatialSize,
                  std::function<void(const acoustical_mode_profile*)> onApplyMode,
                  std::function<std::string()> activeModeId,
                  std::function<void()> onModeCleared)
        : engine_(engine), phone_(phone), onGeneratorActive_(std::move(onGeneratorActive)),
          spatialX_(spatialX), spatialY_(spatialY), spatialZ_(spatialZ), spatialSize_(spatialSize),
          onApplyMode_(std::move(onApplyMode)), activeModeId_(std::move(activeModeId)),
          onModeCleared_(std::move(onModeCleared)) {
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
            c->setBounds(0, 0, 900, contentHeight_);
    }

    void refresh() { rebuild(); }

    // El timer de la consola lo llama a 30 Hz: la lectura de compensación se
    // recalcula de los atómicos (misma ley que el audio), la etiqueta de
    // posición se refresca y el joystick sigue a los atómicos cuando no está
    // siendo arrastrado (modo/sync/undo lo mueven desde fuera).
    void tickSpatialReadout() {
        updateSpatialReadout();
        updatePositionLabel();
        if (joy_ && !joy_->isDragging())
            joy_->setPositionFromOutside(spatialX_.load(std::memory_order_relaxed),
                                          spatialY_.load(std::memory_order_relaxed));
    }

    // v1.11.0 (lote 3): el timer de la consola lo llama a 30 Hz con el SPL del
    // último análisis (ya copiado bajo el lock del motor; aquí solo se pinta) y
    // la última sonda del auto-chequeo (null = sin sondas todavía). Refresca el
    // gauge de nivel con su pastilla "Objetivo alcanzado", la lectura de la
    // última sonda y el mini-espectro de la referencia capturada.
    void tickLive(float currentSpl, const acoustical::ProbeResult* lastProbe) {
        if (splGauge_)
            splGauge_->setValues(currentSpl, engine_.config().targetSpl, engine_.isRunning());
        if (probeLabel_) {
            if (lastProbe) {
                probeLabel_->setText(
                    juce::String::fromUTF8("Última sonda: ") + juce::String(lastProbe->spl, 1)
                    + juce::String::fromUTF8(" dB · RT60 ") + juce::String(lastProbe->rt60Ms, 0)
                    + juce::String::fromUTF8(" ms · corrección activa ")
                    + juce::String(lastProbe->correctionApplied * 100.0f, 0)
                    + juce::String::fromUTF8(" % (") + juce::String(lastProbe->id) + ")",
                    juce::dontSendNotification);
            } else {
                probeLabel_->setText(juce::String::fromUTF8(
                    "Sin sondas todavía: activa el auto-chequeo (la primera llega a su intervalo)."),
                    juce::dontSendNotification);
            }
        }
        if (refStatus_) {
            const bool captured = engine_.isReferenceCaptured();
            refStatus_->setText(captured
                                    ? juce::String::fromUTF8("Estado: Activa — la corrección compara la sala contra la referencia capturada.")
                                    : juce::String::fromUTF8("Estado: Sin capturar — sin referencia, la corrección usa objetivo plano (0 dB en todas las bandas)."),
                             juce::dontSendNotification);
        }
        if (refSpectrum_)
            refSpectrum_->setReference(engine_.isReferenceCaptured(),
                                        engine_.referenceBands(),
                                        engine_.bandFrequencies());
    }

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

    // v1.11.0 (lote 3): lo mismo pero para la config de trabajo (WorkConfig:
    // fuente de referencia, auto-chequeo y profundidad de bits) — el motor la
    // consume en su tick; el audio no la toca.
    template <typename F>
    void mutateWork(F f) {
        if (onBeforeChange) onBeforeChange();
        auto w = engine_.workConfig();
        f(w);
        engine_.setWorkConfig(w);
        if (onAfterChange) onAfterChange();
    }

    // v1.11.0 (lote 3): retardo fino — escribe el valor (0–2000 ms, paso
    // 0,01) en la config, refresca la lectura tiempo+distancia y mueve el
    // slider sin disparar un segundo mutate (delaySyncing_ lo enmudece).
    void setDelayMs(double v) {
        v = juce::jlimit(0.0, 2000.0, std::round(v * 100.0) / 100.0);
        mutate([v](auto& c) { c.audioDelayMs = static_cast<float>(v); });
        updateDelayReadout(v);
        if (delaySlider_) {
            delaySyncing_ = true;
            delaySlider_->setValue(v, juce::sendNotification);
            delaySyncing_ = false;
        }
    }

    // Lectura "12,45 ms · 4,27 m" (343 m/s; cm para distancias cortas):
    // espejo de formatDelayDistance() del móvil.
    void updateDelayReadout(double v) {
        if (!delayReadout_) return;
        const float meters = static_cast<float>(v) * 0.343f;
        const juce::String dist = meters >= 1.0f
            ? juce::String(meters, 2) + juce::String::fromUTF8(" m")
            : juce::String(meters * 100.0f, 1) + juce::String::fromUTF8(" cm");
        delayReadout_->setText(juce::String(v, 2) + juce::String::fromUTF8(" ms · ") + dist,
                                juce::dontSendNotification);
    }

    // Descripción de la fuente de referencia elegida (una línea, como el móvil).
    void updateRefDescription(acoustical::ReferenceSource src) {
        if (!refDesc_) return;
        const juce::String d =
            src == acoustical::ReferenceSource::PresetSequence
                ? juce::String::fromUTF8("Secuencia predefinida: test automático con tonos y barridos calibrados.")
            : src == acoustical::ReferenceSource::SineSweep
                ? juce::String::fromUTF8("Barrido de seno: la señal de prueba barre de 20 Hz a 20 kHz como referencia de sala.")
            : src == acoustical::ReferenceSource::PinkNoise
                ? juce::String::fromUTF8("Ruido rosa: nivel calibrado de ruido rosa como referencia.")
                : juce::String::fromUTF8("Entrada externa: archivo o consola — la señal llega por el canal de entrada (puente ASIO) y se compara con la salida.");
        refDesc_->setText(d, juce::dontSendNotification);
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
        boxes_.getLast()->setBounds(4, groupStartY_ - 2, 892, y_ - groupStartY_ + 14);
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
        t->setBounds(24, y_, 850, 24);
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
        lab->setBounds(16, y_, 300, 22);
        // El control se añade AL contenido: sin este addAndMakeVisible la caja
        // de texto / slider / combo queda creada (con sus bounds) pero invisible —
        // así "Código del móvil" no se veía nunca (bug desde v1.2.0).
        content().addAndMakeVisible(ctl);
        ctl->setBounds(340, y_ - 2, 546, 26);
    }

    void endRow() { y_ += 34; }

    // === Panel ===

    void rebuild() {
        widgets_.clear(false);
        rowLabels_.clear(false);
        boxes_.clear(false);
        groupOpen_ = false;
        y_ = 8;
        content().removeAllChildren();

        const auto& cfg = engine_.config();

        addModoRow();
        addWorkModesRow();
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
        addCombo(juce::String::fromUTF8("Intervalo de análisis"), {"25 ms", "50 ms", "100 ms", "200 ms", "500 ms"},
                 static_cast<int>(cfg.analysisInterval), [this](int i) {
                     mutate([i](auto& c) {
                         c.analysisInterval = static_cast<acoustical::AnalysisInterval>(i);
                     });
                 });
        endRow();
        addCombo("Bandas", {"8", "10", "16", juce::String::fromUTF8("31 · 1/3 oct"), juce::String::fromUTF8("124 · Ultra")},
                 static_cast<int>(cfg.bandCount), [this](int i) {
                     mutate([i](auto& c) {
                         c.bandCount = static_cast<acoustical::BandCount>(i);
                     });
                 });
        endRow();
        addSlider(juce::String::fromUTF8("Ganancia máxima"), 1, 50, 0.5, cfg.maxGainDb, [this](double v) {
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
        addToggle(juce::String::fromUTF8("Corrección activada"), cfg.correctionEnabled, [this](bool on) {
            mutate([on](auto& c) { c.correctionEnabled = on; });
        });
        endRow();
        addToggle(juce::String::fromUTF8("Sustracción de ruido"), cfg.noiseSubtractionEnabled, [this](bool on) {
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
        // v1.11.0 (lote 3): retardo fino 0–2000 ms con pasos de 0,01 ms
        // (botones ±), lectura tiempo+distancia y presets de escenarios
        // comunes — espejo del FineDelayControl del móvil (pantalla
        // Calibración > "Retardo de audio").
        {
            auto* lab = new juce::Label({}, juce::String::fromUTF8("Retardo fino (ms)"));
            lab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(lab);
            content().addAndMakeVisible(lab);
            lab->setBounds(16, y_, 300, 22);
            auto* ds = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            ds->setNormalisableRange(juce::NormalisableRange<double>(0.0, 2000.0, 0.01));
            ds->setValue(cfg.audioDelayMs, juce::dontSendNotification);
            ds->setNumDecimalPlacesToDisplay(2);
            ds->setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 22);
            ds->onValueChange = [this, ds] {
                if (!ds) return;
                if (delaySyncing_) return;  // actualización programada desde un botón: el mutate ya ocurrió
                const double v = ds->getValue();
                mutate([v](auto& c) { c.audioDelayMs = static_cast<float>(v); });
                updateDelayReadout(v);
            };
            widgets_.add(ds);
            content().addAndMakeVisible(ds);
            ds->setBounds(340, y_ - 2, 380, 26);
            delaySlider_ = ds;
            auto* minus = addButton(juce::String::fromUTF8("−"), [this] {
                setDelayMs(engine_.config().audioDelayMs - 0.01);
            });
            minus->setBounds(726, y_ - 2, 36, 26);
            auto* plus = addButton(juce::String::fromUTF8("+"), [this] {
                setDelayMs(engine_.config().audioDelayMs + 0.01);
            });
            plus->setBounds(768, y_ - 2, 36, 26);
            endRow();
            // Lectura tiempo + distancia (343 m/s; cm para retardos cortos).
            delayReadout_ = new juce::Label({}, juce::String());
            delayReadout_->setColour(juce::Label::textColourId, theme::primary);
            content().addAndMakeVisible(delayReadout_);
            delayReadout_->setBounds(16, y_, 400, 18);
            updateDelayReadout(cfg.audioDelayMs);
            y_ += 22;
            // Presets de escenarios (mismo juego que la fila del móvil).
            auto* presetLab = new juce::Label({}, juce::String::fromUTF8("Presets:"));
            presetLab->setColour(juce::Label::textColourId, theme::textDim);
            content().addAndMakeVisible(presetLab);
            presetLab->setBounds(16, y_, 70, 22);
            const double delayPresets[5] = {25.0, 50.0, 100.0, 250.0, 500.0};
            for (int i = 0; i < 5; ++i) {
                const double p = delayPresets[i];
                auto* b = addButton(juce::String(p, 0), [this, p] { setDelayMs(p); });
                b->setBounds(96 + i * 88, y_, 84, 24);
            }
            y_ += 32;
        }
        addSlider(juce::String::fromUTF8("Calibración SPL (dB)"), -20, 20, 0.5, engine_.splCalibrationOffset(), [this](double v) {
            engine_.setSplCalibrationOffset(static_cast<float>(v));
        }, 1.0, 1);
        endRow();

        // --- Nivel SPL (v1.11.0, lote 3): gauge + objetivo + presets ---
        // El slider "SPL objetivo" se traslada aquí (antes en "Trabajo"): el
        // gauge (izquierda) muestra el nivel actual con su marcador, la
        // pastilla "Objetivo alcanzado" se enciende mientras el nivel llega al
        // objetivo con el motor en marcha, y los presets (derecha) escriben el
        // mismo targetSpl del motor (el móvil tiene los mismos cinco).
        addGroup(juce::String::fromUTF8("Nivel SPL"));
        {
            splGauge_ = new SplGauge();
            widgets_.add(splGauge_);
            content().addAndMakeVisible(splGauge_);
            splGauge_->setBounds(16, y_ - 4, 240, 240);
            auto* lab = new juce::Label({}, juce::String::fromUTF8("SPL objetivo (dB)"));
            lab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(lab);
            content().addAndMakeVisible(lab);
            lab->setBounds(280, y_ + 8, 180, 22);
            auto* ts = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            ts->setNormalisableRange(juce::NormalisableRange<double>(30.0, 110.0, 0.1));
            ts->setValue(cfg.targetSpl, juce::dontSendNotification);
            ts->setNumDecimalPlacesToDisplay(1);
            ts->setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 22);
            ts->onValueChange = [this, ts] {
                if (!ts) return;
                const double v = ts->getValue();
                mutate([v](auto& c) { c.targetSpl = static_cast<float>(v); });
            };
            widgets_.add(ts);
            content().addAndMakeVisible(ts);
            ts->setBounds(470, y_ + 6, 414, 26);
            const double splPresets[5] = {65.0, 75.0, 85.0, 97.0, 103.0};
            for (int i = 0; i < 5; ++i) {
                const double p = splPresets[i];
                auto* b = addButton(juce::String(p, 0), [this, p] {
                    mutate([p](auto& c) { c.targetSpl = static_cast<float>(p); });
                });
                b->setBounds(470 + i * 84, y_ + 44, 80, 24);
            }
            y_ += 248;
        }

        // --- Auto-chequeo (v1.11.0, lote 3): sonda periódica del motor ---
        // El motor mide a la cadencia configurada (intervalo 10–600 s) con la
        // calidad elegida (FFT/bandas propias de la sonda) y la UI muestra la
        // última lectura; espejo del AutoCheckConfig del móvil.
        addGroup(juce::String::fromUTF8("Auto-chequeo (sonda periódica)"));
        {
            const auto wc = engine_.workConfig();
            addToggle(juce::String::fromUTF8("Auto-chequeo activado"), wc.autoCheck.enabled, [this](bool on) {
                mutateWork([on](auto& w) { w.autoCheck.enabled = on; });
            });
            endRow();
            addSlider(juce::String::fromUTF8("Intervalo (s)"), 10, 600, 5,
                    static_cast<double>(wc.autoCheck.intervalSeconds), [this](double v) {
                        mutateWork([v](auto& w) { w.autoCheck.intervalSeconds = static_cast<int>(v); });
                    }, 1.0, 0);
            endRow();
            addCombo(juce::String::fromUTF8("Calidad"),
                    {juce::String::fromUTF8("Rápida (1K / 10 bandas)"),
                     juce::String::fromUTF8("Normal (2K / 16 bandas)"),
                     juce::String::fromUTF8("Alta (4K / 31 bandas)")},
                    static_cast<int>(wc.autoCheck.quality), [this](int i) {
                        mutateWork([i](auto& w) {
                            w.autoCheck.quality = static_cast<acoustical::ProbeQuality>(i);
                        });
                    });
            endRow();
            addCombo(juce::String::fromUTF8("Profundidad de bits"),
                    {juce::String::fromUTF8("16-bit"), juce::String::fromUTF8("24-bit"),
                     juce::String::fromUTF8("32-bit (float)")},
                    static_cast<int>(wc.bitDepth), [this](int i) {
                        mutateWork([i](auto& w) {
                            const auto bd = static_cast<acoustical::BitDepth>(i);
                            w.bitDepth = bd;
                            w.autoCheck.bitDepth = bd;
                        });
                    });
            endRow();
            probeLabel_ = new juce::Label({}, juce::String());
            probeLabel_->setColour(juce::Label::textColourId, theme::primary);
            content().addAndMakeVisible(probeLabel_);
            probeLabel_->setBounds(24, y_, 850, 20);
            probeLabel_->setText(juce::String::fromUTF8(
                "Sin sondas todavía: activa el auto-chequeo (la primera llega a su intervalo)."),
                                 juce::dontSendNotification);
            endRow();
        }

        // --- Referencia (v1.11.0, lote 3): fuente de señal y espectro ---
        // Espejo de la sección "Referencia" del móvil: elección de fuente
        // (WorkConfig.referenceSource), estado de la captura y el espectro de
        // bandas capturadas (lo que la corrección compara contra la sala).
        addGroup(juce::String::fromUTF8("Referencia (fuente y espectro)"));
        {
            const auto wc = engine_.workConfig();
            addCombo(juce::String::fromUTF8("Fuente"),
                    {juce::String::fromUTF8("Secuencia predefinida"),
                     juce::String::fromUTF8("Barrido de seno"),
                     juce::String::fromUTF8("Ruido rosa"),
                     juce::String::fromUTF8("Entrada externa (archivo / consola)")},
                    static_cast<int>(wc.referenceSource), [this](int i) {
                        const auto src = static_cast<acoustical::ReferenceSource>(i);
                        mutateWork([src](auto& w) { w.referenceSource = src; });
                        updateRefDescription(src);
                    });
            endRow();
            refDesc_ = new juce::Label({}, juce::String());
            refDesc_->setColour(juce::Label::textColourId, theme::textDim);
            refDesc_->setJustificationType(juce::Justification::centredLeft);
            content().addAndMakeVisible(refDesc_);
            refDesc_->setBounds(16, y_, 870, 18);
            updateRefDescription(wc.referenceSource);
            y_ += 22;
            refStatus_ = new juce::Label({}, juce::String());
            refStatus_->setColour(juce::Label::textColourId, theme::textPrimary);
            refStatus_->setJustificationType(juce::Justification::centredLeft);
            content().addAndMakeVisible(refStatus_);
            refStatus_->setBounds(16, y_, 870, 18);
            y_ += 24;
            auto* cap = addButton(juce::String::fromUTF8("Capturar referencia"), [this] {
                engine_.captureReference();
            });
            cap->setBounds(16, y_, 150, 24);
            auto* clr = addButton(juce::String::fromUTF8("Borrar"), [this] {
                engine_.clearReference();
            });
            clr->setBounds(174, y_, 90, 24);
            endRow();
            refSpectrum_ = new RefSpectrum();
            widgets_.add(refSpectrum_);
            content().addAndMakeVisible(refSpectrum_);
            refSpectrum_->setBounds(16, y_, 856, 96);
            y_ += 104;
        }

        // --- Espacio (v1.11.0: panel como la pantalla "Control" del móvil:
        // joystick para x/y + faders verticales de distancia/tamaño +
        // entorno; la lectura de compensación queda igual, abajo) ---
        addGroup(juce::String::fromUTF8("Espacio — posición del elemento y compensación"));
        {
            auto* lab = new juce::Label({}, juce::String::fromUTF8(
                "Posición: arrastra dentro del círculo (izquierda/derecha · frente/detrás)"));
            lab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(lab);
            content().addAndMakeVisible(lab);
            lab->setBounds(16, y_, 300, 22);
            joy_ = new SpatialJoystick([this](float x, float y) {
                spatialX_.store(x, std::memory_order_relaxed);
                spatialY_.store(y, std::memory_order_relaxed);
                updateSpatialReadout();
                updatePositionLabel();
            });
            joy_->setEnvironment(currentEnv_);
            widgets_.add(joy_);
            content().addAndMakeVisible(joy_);
            joy_->setBounds(340, y_ - 6, 208, 208);
            auto* envCombo = new juce::ComboBox();
            envCombo->setColour(juce::ComboBox::backgroundColourId, theme::surface);
            envCombo->setColour(juce::ComboBox::textColourId, theme::textPrimary);
            envCombo->setColour(juce::ComboBox::arrowColourId, theme::textDim);
            envCombo->addItem(juce::String::fromUTF8("Sala (8×10 m)"), 1);
            envCombo->addItem(juce::String::fromUTF8("Estudio (6×7 m)"), 2);
            envCombo->addItem(juce::String::fromUTF8("Escenario (12×8 m)"), 3);
            envCombo->addItem(juce::String::fromUTF8("Exterior (20×30 m)"), 4);
            envCombo->setSelectedItemIndex(static_cast<int>(currentEnv_) + 1, juce::dontSendNotification);
            envCombo->onChange = [this, envCombo] {
                currentEnv_ = static_cast<SpatialEnvironment>(envCombo->getSelectedItemIndex() - 1);
                if (joy_) joy_->setEnvironment(currentEnv_);
                updatePositionLabel();
            };
            widgets_.add(envCombo);
            content().addAndMakeVisible(envCombo);
            envCombo->setBounds(568, y_, 318, 26);
            positionLabel_ = new juce::Label({}, juce::String());
            positionLabel_->setColour(juce::Label::textColourId, theme::textPrimary);
            positionLabel_->setJustificationType(juce::Justification::centredLeft);
            content().addAndMakeVisible(positionLabel_);
            positionLabel_->setBounds(568, y_ + 34, 318, 22);
            updatePositionLabel();
            y_ += 224;
            // Faders verticales (móvil: "Traer aquí / llevar" y "Tamaño equipo")
            auto* distLab = new juce::Label({}, juce::String::fromUTF8(
                "Distancia (cerca ↔ lejos)"));
            distLab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(distLab);
            content().addAndMakeVisible(distLab);
            distLab->setBounds(340, y_, 130, 20);
            auto* ds = new juce::Slider(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
            ds->setNormalisableRange(juce::NormalisableRange<double>(0.0, 15.0, 0.05));
            ds->setValue(spatialZ_.load() * 15.0, juce::dontSendNotification);
            ds->setNumDecimalPlacesToDisplay(1);
            ds->setColour(juce::Slider::backgroundColourId, theme::background);
            ds->setColour(juce::Slider::trackColourId, theme::primaryDark);
            ds->setColour(juce::Slider::thumbColourId, theme::primary);
            ds->setColour(juce::Slider::textBoxTextColourId, theme::textPrimary);
            ds->setColour(juce::Slider::textBoxBackgroundColourId, theme::surface);
            ds->onValueChange = [this, ds] {
                if (!ds) return;
                const double v = ds->getValue();
                spatialZ_.store(static_cast<float>(v / 15.0), std::memory_order_relaxed);
                updateSpatialReadout();
            };
            distSlider_ = ds;
            widgets_.add(ds);
            content().addAndMakeVisible(ds);
            ds->setBounds(340, y_ + 24, 40, 112);
            auto* sizeLab = new juce::Label({}, juce::String::fromUTF8("Tamaño del elemento"));
            sizeLab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(sizeLab);
            content().addAndMakeVisible(sizeLab);
            sizeLab->setBounds(500, y_, 130, 20);
            auto* ss = new juce::Slider(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
            ss->setNormalisableRange(juce::NormalisableRange<double>(0.0, 1.0, 0.05));
            ss->setValue(spatialSize_.load(), juce::dontSendNotification);
            ss->setNumDecimalPlacesToDisplay(2);
            ss->setColour(juce::Slider::backgroundColourId, theme::background);
            ss->setColour(juce::Slider::trackColourId, theme::primaryDark);
            ss->setColour(juce::Slider::thumbColourId, theme::primary);
            ss->setColour(juce::Slider::textBoxTextColourId, theme::textPrimary);
            ss->setColour(juce::Slider::textBoxBackgroundColourId, theme::surface);
            ss->onValueChange = [this, ss] {
                if (!ss) return;
                const double v = ss->getValue();
                spatialSize_.store(static_cast<float>(v), std::memory_order_relaxed);
                updateSpatialReadout();
                updateSizeName();
            };
            sizeSlider_ = ss;
            widgets_.add(ss);
            content().addAndMakeVisible(ss);
            ss->setBounds(500, y_ + 24, 40, 112);
            sizeName_ = new juce::Label({}, juce::String());
            sizeName_->setColour(juce::Label::textColourId, theme::textDim);
            sizeName_->setJustificationType(juce::Justification::centredLeft);
            content().addAndMakeVisible(sizeName_);
            sizeName_->setBounds(548, y_ + 136, 120, 18);
            updateSizeName();
            y_ += 158;
        }
        spatialReadout_ = new juce::Label({}, juce::String());
        spatialReadout_->setColour(juce::Label::textColourId, theme::textDim);
        spatialReadout_->setBounds(16, y_, 870, 22);
        updateSpatialReadout();
        content().addAndMakeVisible(spatialReadout_);
        endRow();

        // --- Generador de señales ---
        addGroup(juce::String::fromUTF8("Generador de señales"));
        // La forma de onda actual se refleja (antes el combo siempre mostraba
        // "Silencio" aunque el generador sonara). Con el combo se controla TODO:
        // elegir otra cosa que Silencio enciende el generador (sale por el EQ y
        // por el puente ASIO); "Silencio" lo apaga y la entrada pasa tal cual.
        {
            const auto w = engine_.signalGenerator().waveform();
            const int selected = comboIndexForWaveform(w);
            addCombo(juce::String::fromUTF8("Señal (Silencio = sin señal añadida: la entrada pasa tal cual)"),
                     {juce::String::fromUTF8("Silencio"), juce::String::fromUTF8("Seno"),
                      juce::String::fromUTF8("Seno por banda"), juce::String::fromUTF8("Barrido log"),
                      juce::String::fromUTF8("Ruido rosa"), juce::String::fromUTF8("Ruido blanco")},
                     selected, [this](int i) {
                auto& gen = engine_.signalGenerator();
                const auto w = waveformForComboIndex(i);
                gen.setWaveform(w);
                if (onGeneratorActive_) onGeneratorActive_(w != acoustical::SignalGenerator::Waveform::Silence);
            });
        }
        endRow();
        addSlider("Frecuencia", 20, 20000, 1, engine_.signalGenerator().frequency(), [this](double v) {
            engine_.signalGenerator().setFrequency(static_cast<float>(v));
        }, 0.25, 1);
        endRow();
        addSlider("Nivel", -40, 0, 0.5, engine_.signalGenerator().levelDb(), [this](double v) {
            engine_.signalGenerator().setLevelDb(static_cast<float>(v));
        }, 1.0, 1);
        endRow();

        endGroupIfOpen();

        // --- Móvil (Wi-Fi / USB) ---
        addPhoneGroup();
        endGroupIfOpen();

        contentHeight_ = y_ + 16;
        resized();
    }

    // === Móvil (Wi-Fi / USB) ===

    void styleEditor(juce::TextEditor* ed) {
        ed->setMultiLine(false, false);
        ed->setColour(juce::TextEditor::backgroundColourId, theme::surface);
        ed->setColour(juce::TextEditor::textColourId, juce::Colours::whitesmoke);
        ed->setColour(juce::TextEditor::outlineColourId, theme::surfaceHi);
    }

    void addPhoneGroup() {
        addGroup(juce::String::fromUTF8("Móvil (Wi-Fi / USB)"));
        auto* codeEd = new juce::TextEditor();
        styleEditor(codeEd);
        codeEd->setText(phone_ ? phone_->pairCode() : juce::String(), false);
        widgets_.add(codeEd);
        addControlRow(juce::String::fromUTF8("Código del móvil"), codeEd);
        endRow();
        auto* ipEd = new juce::TextEditor();
        styleEditor(ipEd);
        ipEd->setText(phone_ ? phone_->manualPhoneIp() : juce::String(), false);
        widgets_.add(ipEd);
        addControlRow("IP manual (opcional)", ipEd);
        endRow();
        auto* saveCode = new juce::TextButton(juce::String::fromUTF8("Guardar código"));
        saveCode->onClick = [this, codeEd] {
            if (phone_) phone_->setPairCode(codeEd->getText().trim());
        };
        widgets_.add(saveCode);
        content().addAndMakeVisible(saveCode);
        saveCode->setBounds(340, y_ - 6, 180, 30);
        auto* saveIp = new juce::TextButton("Guardar IP");
        saveIp->onClick = [this, ipEd] {
            if (phone_) phone_->setManualPhoneIp(ipEd->getText().trim());
        };
        widgets_.add(saveIp);
        content().addAndMakeVisible(saveIp);
        saveIp->setBounds(536, y_ - 6, 180, 30);
        endRow();
        // Ajustes en ambos sentidos: el botón "Sincronizar" de la barra superior
        // manda PC → móvil; este toma la config actual del móvil y la aplica al PC.
        {
            auto* lab = new juce::Label({}, juce::String::fromUTF8("Ajustes del móvil"));
            lab->setColour(juce::Label::textColourId, theme::textDim);
            rowLabels_.add(lab);
            content().addAndMakeVisible(lab);
            lab->setBounds(16, y_, 300, 22);
            auto* recv = new juce::TextButton(juce::String::fromUTF8("Recibir del móvil"));
            recv->setTooltip(juce::String::fromUTF8(
                "Toma los ajustes que tiene ahora el móvil y los aplica al PC "
                "(el móvil también puede enviarlos desde su app)"));
            recv->onClick = [this] { if (onReceiveFromPhone) onReceiveFromPhone(); };
            widgets_.add(recv);
            content().addAndMakeVisible(recv);
            recv->setBounds(340, y_ - 6, 180, 30);
        }
        endRow();
        // Pista en tres líneas cortas: una sola línea de ~200 caracteres se
        // recortaba en el panel estrecho.
        auto* hint1 = new juce::Label(
            {}, juce::String::fromUTF8("El código aparece en la app del móvil (Ajustes > PC/Windows): pégalo aquí "
                "una sola vez y el PC encontrará al móvil por Wi-Fi solo."));
        auto* hint2 = new juce::Label(
            {}, juce::String::fromUTF8("La IP manual es solo por si tu red bloquea la baliza; el cable USB queda "
                "de respaldo."));
        auto* hint3 = new juce::Label(
            {}, juce::String::fromUTF8("Ajustes: el botón Sincronizar (barra superior) manda PC → móvil; "
                "«Recibir del móvil» hace lo contrario."));
        hint1->setFont(juce::Font(12.0f));
        hint2->setFont(juce::Font(12.0f));
        hint3->setFont(juce::Font(12.0f));
        hint1->setColour(juce::Label::textColourId, theme::textDim);
        hint2->setColour(juce::Label::textColourId, theme::textDim);
        hint3->setColour(juce::Label::textColourId, theme::textDim);
        hint1->setJustificationType(juce::Justification::centredLeft);
        hint2->setJustificationType(juce::Justification::centredLeft);
        hint3->setJustificationType(juce::Justification::centredLeft);
        rowLabels_.add(hint1);
        rowLabels_.add(hint2);
        rowLabels_.add(hint3);
        content().addAndMakeVisible(hint1);
        content().addAndMakeVisible(hint2);
        content().addAndMakeVisible(hint3);
        hint1->setBounds(24, y_ + 2, 852, 16);
        hint2->setBounds(24, y_ + 20, 852, 16);
        hint3->setBounds(24, y_ + 38, 852, 16);
        y_ += 68;
    }

    // === Modos rápidos ===

    void addModoRow() {
        addGroup(juce::String::fromUTF8("Modos rápidos — toca, escucha y afina después a tu gusto"));
        const juce::String names[3] = {juce::String::fromUTF8("Salón (música)"),
                                       juce::String::fromUTF8("Cine (graves)"),
                                       juce::String::fromUTF8("Estudio (precisión)")};
        for (int m = 0; m < 3; ++m) {
            auto* b = addButton(names[m], [this, m] { applyMode(m); });
            b->setBounds(16 + m * 292, y_, 272, 32);
        }
        endRow();
    }

    // === Modos de trabajo (los 6 perfiles de arranque del catálogo C) ===
    // El catálogo vive en el núcleo compartido (dsp/acoustical_dsp.c): la
    // app, el plugin VST y la app móvil (copia Kotlin espejo) arrancan con
    // los MISMOS valores. Pulsa un modo y el panel entero se re-dibuja con
    // su config; todo sigue editable y el modo activo se guarda solo.
    void addWorkModesRow() {
        addGroup(juce::String::fromUTF8("Modos de trabajo — config completa de arranque (SPL, ecuas, espacio)"));
        const auto* profiles = acoustical_mode_profiles();
        const std::string active = activeModeId_ ? activeModeId_() : std::string();
        for (int i = 0; i < ACOUSTICAL_MODE_COUNT; ++i) {
            const auto* m = &profiles[i];
            const bool isActive = active == m->id;
            juce::String label = juce::String::fromUTF8(m->name);
            if (isActive) label = juce::String::fromUTF8("● ") + label;
            auto* b = addButton(label, [this, m] { if (onApplyMode_) onApplyMode_(m); });
            b->setBounds(16 + (i % 2) * 446, y_ + (i / 2) * 34, 430, 30);
            // El activo se distingue también por color de texto. JUCE 7: el
            // textColourId único de la v6 se partió en on/off (sin estado
            // "on" un TextButton es siempre off, pero se pintan ambos por
            // si el LookAndFeel lo conmuta).
            if (isActive) {
                b->setColour(juce::TextButton::textColourOnId, theme::primary);
                b->setColour(juce::TextButton::textColourOffId, theme::primary);
            }
        }
        // 3 filas (2 columnas): reserva el alto del bloque
        for (int r = 0; r < 3; ++r) endRow();
        // Nota de ayuda bajo los botones
        auto* note = new juce::Label({}, juce::String::fromUTF8(
            "Cada modo fija motor (SPL, ganancia, bandas, suavizado), los 3 ecuas dinámicos con sus bandas de apoyo "
            "y la posición espacial. El modo activo se guarda y se vuelve a cargar al arrancar."));
        note->setFont(juce::Font(12.0f));
        note->setColour(juce::Label::textColourId, theme::textDim);
        note->setJustificationType(juce::Justification::centredLeft);
        note->setBounds(24, y_, 852, 32);
        rowLabels_.add(note);
        content().addAndMakeVisible(note);
        y_ += 44;
    }

    // Mapeo combo "Señal" <-> enum del generador (el orden del combo es el
    // del menú de las 3 versiones: 0 = Silencio, primero, y el único que no
    // suena).
    static acoustical::SignalGenerator::Waveform waveformForComboIndex(int i) {
        switch (i) {
            case 0:  return acoustical::SignalGenerator::Waveform::Silence;
            case 1:  return acoustical::SignalGenerator::Waveform::Sine;
            case 2:  return acoustical::SignalGenerator::Waveform::BandSine;
            case 3:  return acoustical::SignalGenerator::Waveform::LogSweep;
            case 4:  return acoustical::SignalGenerator::Waveform::PinkNoise;
            default: return acoustical::SignalGenerator::Waveform::WhiteNoise;
        }
    }
    static int comboIndexForWaveform(acoustical::SignalGenerator::Waveform w) {
        switch (w) {
            case acoustical::SignalGenerator::Waveform::Silence:  return 0;
            case acoustical::SignalGenerator::Waveform::Sine:     return 1;
            case acoustical::SignalGenerator::Waveform::BandSine: return 2;
            case acoustical::SignalGenerator::Waveform::LogSweep: return 3;
            case acoustical::SignalGenerator::Waveform::PinkNoise:return 4;
            case acoustical::SignalGenerator::Waveform::WhiteNoise:
            default: return 5;
        }
    }

    // === Espacio: lectura en vivo de la compensación (ley única del core C) ===

    void updateSpatialReadout() {
        if (!spatialReadout_) return;
        float gainDb = 0.0f, delayMs = 0.0f;
        acoustical_spatial_compensate(spatialX_.load(std::memory_order_relaxed),
                                      spatialY_.load(std::memory_order_relaxed),
                                      spatialZ_.load(std::memory_order_relaxed),
                                      spatialSize_.load(std::memory_order_relaxed),
                                      &gainDb, &delayMs);
        // "cerca, medio, lejos" en metros; la compensación se aplica en la
        // cadena de salida (ganancia + retardo) por la consola.
        const float meters = spatialZ_.load(std::memory_order_relaxed) * 15.0f;
        // JUCE 7: el constructor String(valor, places, padWithZeros) de la
        // v6 desapareció; (valor, decimales) pinta lo mismo para estos
        // rangos (0..15). Y fromUTF8 ya no tiene overload de String (solo
        // const char*): la suma ya es un String, no hace falta envolverla.
        spatialReadout_->setText(
            juce::String(gainDb, 1) + juce::String::fromUTF8(" dB de ganancia  ·  ")
            + juce::String(delayMs, 1) + juce::String::fromUTF8(" ms de retardo  ·  ")
            + juce::String(meters, 1) + juce::String::fromUTF8(" m de distancia (7,5 m centrado y tamaño medio = sin compensación)"),
            juce::dontSendNotification);
    }

    // v1.11.0: etiqueta de posición ("Centro" / "Izq 1,2 m · Frente 3,4 m"),
    // espejo de positionLabel del móvil, en metros del entorno elegido.
    void updatePositionLabel() {
        if (!positionLabel_) return;
        positionLabel_->setText(
            spatialPositionLabel(spatialX_.load(std::memory_order_relaxed),
                                 spatialY_.load(std::memory_order_relaxed),
                                 currentEnv_),
            juce::dontSendNotification);
    }

    // v1.11.0: nombre del tamaño (móvil: Pequeño/Mediano/Grande/Muy grande).
    void updateSizeName() {
        if (!sizeName_) return;
        const float s = spatialSize_.load(std::memory_order_relaxed);
        const juce::String name =
            s < 0.25f ? juce::String::fromUTF8("Pequeño")
            : s < 0.5f ? juce::String::fromUTF8("Mediano")
            : s < 0.75f ? juce::String::fromUTF8("Grande")
                         : juce::String::fromUTF8("Muy grande");
        sizeName_->setText(name, juce::dontSendNotification);
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
        // Un modo rápido no es un modo del catálogo: si había uno activo, ya no
        // lo es (des-marcarlo en el rebuild de abajo).
        if (onModeCleared_) onModeCleared_();
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
        return juce::String::fromUTF8("PASO A PASO RECOMENDADO\n"
            "1. Pestaña Audio: elige tu tarjeta de entrada y salida (altavoces, HDMI,\n"
            "    Bluetooth, USB o interface). Ahí también van la frecuencia de muestreo\n"
            "    y el tamaño de buffer.\n"
            "2. Pulsa Ruido (barra superior) en silencio: aprende el fondo de\n"
            "    tu sala y activa la sustracción de ruido.\n"
            "3. Dale a Referencia: el motor toma el nivel de cada banda y\n"
            "    empieza a corregir solo.\n"
            "4. Los tres ecuas dinámicos trabajan solos: cian (donde más se necesita),\n"
            "    ámbar (graves) y magenta (agudos). Actívalos con F1, F2 y F3.\n\n"
            "MODOS RÁPIDOS\n"
            "Salón: corrección musical equilibrada (31 bandas, 100 ms).\n"
            "Cine: deja subir más los graves y reacciona más lento (18 dB, 200 ms).\n"
            "Estudio: máxima fidelidad, poca intervención (6 dB, 50 ms, FFT 8K).\n"
            "Después del modo, TODO es editable: los cambios se guardan solos.\n\n"
            "MODOS DE TRABAJO (v1.8)\n"
            "Seis perfiles completos de arranque (Coche Normal / Coche Aire libre /\n"
            "Estudio-Escenario 1 / Escenarios 2 y 3 / PAs) con SPL objetivo, motor,\n"
            "los 3 ecuas con sus bandas de apoyo y la posición espacial. Pulsa uno,\n"
            "afina lo que quieras (sigue guardándose con ese modo) y al arrancar la\n"
            "app se carga el último modo activo, con los cambios que hayas hecho.\n\n"
            "ESPACIO (v1.8; v1.11.0: panel como en el móvil)\n"
            "Posición del elemento que escuchas: el joystick (arrastrar dentro del\n"
            "círculo) fija izquierda/derecha (x) y frente/detrás (y); los faders\n"
            "verticales, la distancia (z, 0 a 15 m) y el tamaño del elemento.\n"
            "El entorno (sala/estudio/escenario/exterior) define los metros de la\n"
            "lectura y el fondo del joystick.\n"
            "La compensación de ganancia y retardo de propagación se aplica a la\n"
            "salida real, igual que en el móvil y en el plugin VST (misma ley en las\n"
            "tres versiones).\n\n"
            "VALORES EXACTOS\n"
            "Cada deslizador tiene su caja de texto a la derecha: haz clic y escribe el\n"
            "valor exacto (físico o con Teclado en pantalla). Precisión: retardos de\n"
            "0,01 ms, suavizado con 3 decimales, SPL con décimas.\n\n"
            "DESHACER\n"
            "Ctrl+Z deshace cualquier cambio de ajustes, Ctrl+Shift+Z o Ctrl+Y lo rehace.\n"
            "Bloquear evita mover el EQ por un toque accidental.\n"
            "Congelar (barra espaciadora) detiene la pantalla para leer valores;\n"
            "el audio sigue corrigiendo.\n\n"
            "AJUSTES PRINCIPALES\n"
            "Bandas: resolución del corrector (8/10/16/31/124 bandas).\n"
            "Intervalo de análisis: cada cuánto se mide la sala (25-500 ms).\n"
            "Suavizado: cuánto se fía del último análisis (0,05 nervioso - 0,8 estable).\n"
            "Umbral de ruido: por debajo de este nivel no se corrige.\n"
            "Retardo global: alinea el sonido con la imagen (pasos de 0,01 ms).\n\n"
            "GENERADOR DE SEÑALES\n"
            "Seno por banda y barrido log sirven para verificar cada banda a mano;\n"
            "ruido rosa para probar el sistema completo. Sale a través del EQ.\n\n"
            "ENTRADAS (pestaña Ruteos)\n"
            "Elige qué tarjeta escucha el micrófono de referencia (la referencia,\n"
            "el ruido y el auto-alinear usan esa entrada) y ajusta el nivel de cada\n"
            "canal L/R. El selector completo de dispositivos sigue en la pestaña\n"
            "Audio; los dos se reflejan entre sí.\n\n"
            "MÓVIL (Wi-Fi / USB)\n"
            "El móvil y el PC están en la misma red Wi-Fi: la app del móvil anuncia su\n"
            "dirección y el PC la encuentra solo (no hace falta cable ni escribir IPs).\n"
            "La primera vez, pega en Ajustes > Móvil el código de 6 dígitos que muestra\n"
            "la app del móvil (Ajustes > PC/Windows); queda guardado para siempre.\n"
            "Sin Wi-Fi, el cable USB sigue funcionando de respaldo, como antes.\n"
            "Las actualizaciones de Windows llegan del móvil (con tu permiso, verificadas).");
    }

    // === Ecuas dinámicos ===

    void addDynamicEqGroup(int index) {
        const juce::String n = juce::String(index + 1);
        auto& sweeper = engine_.sweeperFor(index);  // acceso directo a parámetros en vivo

        addGroup(juce::String::fromUTF8("Ecu dinámico ") + n);
        addToggle("Activado", engine_.dynamicEqEnabled(index), [this, index](bool on) {
            engine_.setDynamicEqEnabled(index, on);
        });
        endRow();
        addSlider(juce::String::fromUTF8("Intervalo de decisión (ms)"),
                  acoustical::DynamicEqConfig::MIN_INTERVAL_MS,
                  acoustical::DynamicEqConfig::MAX_INTERVAL_MS, 50,
                  sweeper.decisionIntervalMs.load(), [this, index](double v) {
                      engine_.setDynamicEqInterval(index, static_cast<int>(v));
                  });
        endRow();
        addSlider(juce::String::fromUTF8("Ganancia máxima (dB)"), 1, 50, 0.5, sweeper.maxGainDb.load(),
                  [this, index](double v) {
                      engine_.setDynamicEqMaxGain(index, static_cast<float>(v));
                  }, 1.0, 1);
        endRow();
        addSlider("Mezclador propio", 0, 1, 0.01, sweeper.mixerLevel.load(),
                  [this, index](double v) {
                      engine_.setDynamicEqMixerLevel(index, static_cast<float>(v));
                  });
        endRow();
        addCombo("Velocidad del suavizado", {juce::String::fromUTF8("×0.5"), juce::String::fromUTF8("×1"), juce::String::fromUTF8("×2"), juce::String::fromUTF8("×4")}, 1,
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

        // Bandas de apoyo de frecuencia libre (4 por ecu, igual que en el móvil).
        // El valor inicial es la banda que el motor YA tiene: un modo del catálogo
        // asigna frecuencias distintas por ecu (2.5k–20k / 63–4k / 20–83 Hz) y
        // el panel las refleja al redibujarse; sin modo activo, la serie geométrica
        // de siempre (250/1k/4k/16k).
        // v1.10.0: la Q también es editable (como en el móvil, 0,1–10): el
        // catálogo arranca las bandas a la Q más estrecha (0,1) y tocar un
        // slider de frecuencia/ganancia ya no las reponía a 2,0 de siempre.
        const auto currentBands = engine_.supportBands(index);
        for (int b = 0; b < 4; ++b) {
            const float defFreq = 250.0f * std::pow(4.0f, b);
            const float defGain = 0.0f;
            const float defQ = 2.0f;
            const float initFreq = b < static_cast<int>(currentBands.size()) ? currentBands[b].frequencyHz : defFreq;
            const float initGain = b < static_cast<int>(currentBands.size()) ? currentBands[b].gainDb : defGain;
            const float initQ = b < static_cast<int>(currentBands.size()) ? currentBands[b].q : defQ;
            supportFreq_[index][b] = initFreq;
            supportGain_[index][b] = initGain;
            supportQ_[index][b] = initQ;
            addSlider("Apoyo " + juce::String(b + 1) + juce::String::fromUTF8(" · frecuencia"), 20, 20000, 1,
                      initFreq, [this, index, b](double v) {
                          supportFreq_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      }, 0.25, 1);
            endRow();
            addSlider("Apoyo " + juce::String(b + 1) + juce::String::fromUTF8(" · ganancia (dB)"), -12, 12, 0.25, initGain,
                      [this, index, b](double v) {
                          supportGain_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      }, 1.0, 2);
            endRow();
            addSlider("Apoyo " + juce::String(b + 1) + juce::String::fromUTF8(" · Q"), 0.1, 10, 0.1, initQ,
                      [this, index, b](double v) {
                          supportQ_[index][b] = static_cast<float>(v);
                          pushSupportBands(index);
                      }, 1.0, 1);
            endRow();
        }
    }

    void pushSupportBands(int index) {
        std::vector<acoustical::SupportBand> bands;
        for (int b = 0; b < 4; ++b)
            bands.push_back({supportFreq_[index][b], supportGain_[index][b], supportQ_[index][b]});
        engine_.setSupportBands(index, bands);
    }

    int y_ = 8;
    int contentHeight_ = 400;
    int groupStartY_ = 0;
    bool groupOpen_ = false;

    // v1.11.0 (lote 3): retardo fino (botones ± + presets escriben la config y
    // mueven el slider; delaySyncing_ evita el doble mutate en ese camino).
    juce::Slider* delaySlider_ = nullptr;
    juce::Label* delayReadout_ = nullptr;
    bool delaySyncing_ = false;
    // v1.11.0 (lote 3): grupo "Nivel SPL" — gauge de nivel + pastilla
    // "Objetivo alcanzado" (tickLive lo refresca a 30 Hz).
    SplGauge* splGauge_ = nullptr;
    // v1.11.0 (lote 3): grupo "Auto-chequeo" — lectura de la última sonda.
    juce::Label* probeLabel_ = nullptr;
    // v1.11.0 (lote 3): grupo "Referencia" — descripción de la fuente,
    // estado de la captura y el mini-espectro de la referencia capturada.
    juce::Label* refDesc_ = nullptr;
    juce::Label* refStatus_ = nullptr;
    RefSpectrum* refSpectrum_ = nullptr;

    juce::Viewport scroller_;
    juce::OwnedArray<juce::Component> widgets_;
    juce::OwnedArray<juce::Label> rowLabels_;
    juce::OwnedArray<juce::GroupComponent> boxes_;
    juce::Label* spatialReadout_ = nullptr;  // creada en rebuild() de "Espacio"
    // v1.11.0: el panel de Espacio pasa a tener joystick + faders verticales
    // + entorno (igual que la pantalla "Control" del móvil). El entorno es solo
    // visual (metros del entorno en la lectura y el fondo del joystick); la ley
    // de compensación no depende de él.
    SpatialJoystick* joy_ = nullptr;
    juce::Label* positionLabel_ = nullptr;
    juce::Slider* distSlider_ = nullptr;
    juce::Slider* sizeSlider_ = nullptr;
    juce::Label* sizeName_ = nullptr;
    SpatialEnvironment currentEnv_ = SpatialEnvironment::Room;

    // Referencias al estado espacial de la consola (la UI escribe, el callback
    // de audio lee con load()): nunca se copian, el panel y el audio miran
    // los MISMOS atómicos.
    std::atomic<float>& spatialX_;
    std::atomic<float>& spatialY_;
    std::atomic<float>& spatialZ_;
    std::atomic<float>& spatialSize_;
    std::function<void(const acoustical_mode_profile*)> onApplyMode_;
    std::function<std::string()> activeModeId_;
    // Un modo rápido (Salón/Cine/Estudio) no es un perfil del catálogo: deja
    // de "estar" un modo de trabajo activo (los botones se des-marcan y el
    // arranque vuelve a la config genérica guardada).
    std::function<void()> onModeCleared_;

    float supportFreq_[3][4] = {{250, 1000, 4000, 12000}, {250, 1000, 4000, 12000},
                                {250, 1000, 4000, 12000}};
    float supportGain_[3][4] = {};
    float supportQ_[3][4] = {{2, 2, 2, 2}, {2, 2, 2, 2}, {2, 2, 2, 2}};

    acoustical::AcousticalEngine& engine_;
    PhoneLink* phone_ = nullptr;
    std::function<void(bool)> onGeneratorActive_;
};
