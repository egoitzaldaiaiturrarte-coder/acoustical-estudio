// PluginEditor.h — editor del plugin: autosuficiente, con TODO el inventario
// del móvil editable desde la propia ventana (automatizable vía APVTS).
//  - Barra maestra: corrección, link, ruido, captura de referencia (pre-EQ),
//    gestión del perfil de ruido, ganancia máx, suavizado, SPL objetivo,
//    retardo global, análisis/bandas/FFT y generador de referencia.
//  - EQ grande con las bandas y los highlights de los tres barridos.
//  - Por ecu dinámico: tarjeta en vivo + panel (activo, arranque, intervalo,
//    ganancia, mezclador, velocidad, extras y 4 bandas de apoyo).
#pragma once

#include "PluginProcessor.h"
#include "EqCanvas.h"
#include "DynamicEqCard.h"
#include "Theme.h"

namespace {
// Etiquetas en español seguras para MSVC (fuente UTF-8 con /utf-8).
juce::String utf8(const char* s) { return juce::String::fromUTF8(s); }
} // namespace

// Fila compacta etiqueta + slider, enlazada a un parámetro del APVTS.
class ParamRow : public juce::Component {
public:
    ParamRow(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
             const juce::String& text)
        : label(text, text) {
        label.setFont(juce::Font(11.0f));
        label.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 16);
        slider.setColour(juce::Slider::backgroundColourId, theme::background);
        slider.setColour(juce::Slider::trackColourId, theme::primaryDark);
        slider.setColour(juce::Slider::thumbColourId, theme::primary);
        slider.setColour(juce::Slider::textBoxTextColourId, theme::textPrimary);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, theme::surface);
        addAndMakeVisible(slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, paramId, slider);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        label.setBounds(bounds.removeFromLeft(78));
        slider.setBounds(bounds.reduced(0, 2));
    }

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Interruptor enlazado a un parámetro bool del APVTS.
class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                const juce::String& text)
        : juce::ToggleButton(text) {
        setColour(juce::ToggleButton::textColourId, theme::textPrimary);
        setColour(juce::ToggleButton::tickColourId, theme::primary);
        setColour(juce::ToggleButton::tickDisabledColourId, theme::textDim);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            state, paramId, *this);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Desplegable enlazado a un parámetro de opciones del APVTS.
class ParamCombo : public juce::Component {
public:
    ParamCombo(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
               const juce::String& text, const juce::StringArray& items)
        : label(text, text) {
        label.setFont(juce::Font(11.0f));
        label.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(label);

        combo.setColour(juce::ComboBox::backgroundColourId, theme::surface);
        combo.setColour(juce::ComboBox::textColourId, theme::textPrimary);
        combo.setColour(juce::ComboBox::arrowColourId, theme::textDim);
        combo.addItemList(items, 1);
        addAndMakeVisible(combo);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, paramId, combo);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        label.setBounds(bounds.removeFromLeft(labelWidth_));
        combo.setBounds(bounds);
    }

    void setLabelWidth(int w) { labelWidth_ = w; }

private:
    juce::Label label;
    juce::ComboBox combo;
    int labelWidth_ = 60;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Mini-slider sin etiqueta (para las filas de bandas de apoyo).
class MiniSlider : public juce::Component {
public:
    MiniSlider(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
               int boxWidth) {
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, boxWidth, 14);
        slider.setColour(juce::Slider::backgroundColourId, theme::background);
        slider.setColour(juce::Slider::trackColourId, theme::primaryDark);
        slider.setColour(juce::Slider::thumbColourId, theme::primary);
        slider.setColour(juce::Slider::textBoxTextColourId, theme::textPrimary);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, theme::surface);
        addAndMakeVisible(slider);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            state, paramId, slider);
    }

    void resized() override { slider.setBounds(getLocalBounds()); }

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Fila de banda de apoyo: frecuencia, ganancia y Q en una sola línea.
class SupportBandRow : public juce::Component {
public:
    SupportBandRow(juce::AudioProcessorValueTreeState& state, const juce::String& n,
                   const juce::String& s, const juce::String& title)
        : title_(title, title) {
        title_.setFont(juce::Font(10.0f));
        title_.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(title_);
        freq_ = std::make_unique<MiniSlider>(state, "eq" + n + "Sb" + s + "Freq", 42);
        gain_ = std::make_unique<MiniSlider>(state, "eq" + n + "Sb" + s + "Gain", 40);
        q_ = std::make_unique<MiniSlider>(state, "eq" + n + "Sb" + s + "Q", 32);
        addAndMakeVisible(*freq_);
        addAndMakeVisible(*gain_);
        addAndMakeVisible(*q_);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        title_.setBounds(bounds.removeFromLeft(24));
        freq_->setBounds(bounds.removeFromLeft(bounds.getWidth() - 74).reduced(0, 3));
        gain_->setBounds(bounds.removeFromLeft(44).reduced(0, 3));
        q_->setBounds(bounds.removeFromLeft(30).reduced(0, 3));
    }

private:
    juce::Label title_;
    std::unique_ptr<MiniSlider> freq_, gain_, q_;
};

// Panel de controles de un ecu dinámico (n = "1".."3").
class EqControlsPanel : public juce::Component {
public:
    EqControlsPanel(juce::AudioProcessorValueTreeState& state, const juce::String& n)
        : activo(state, "eq" + n + "Enabled", utf8("Activo")),
          arranque(state, "eq" + n + "Start", utf8("Arranque"),
                   {utf8("Donde más se necesita"), utf8("De graves a agudos"),
                    utf8("De agudos a graves")}),
          intervalo(state, "eq" + n + "Interval", utf8("Intervalo ms")),
          ganancia(state, "eq" + n + "Gain", utf8("Ganancia dB")),
          mezclador(state, "eq" + n + "Mixer", "Mezclador"),
          velocidad(state, "eq" + n + "Speed", "Velocidad"),
          extras(state, "eq" + n + "Extras", "Extras") {
        arranque.setLabelWidth(60);
        addAndMakeVisible(activo);
        addAndMakeVisible(arranque);
        for (auto* row : { &intervalo, &ganancia, &mezclador, &velocidad, &extras })
            addAndMakeVisible(*row);
        for (int k = 1; k <= 4; ++k) {
            const juce::String s(k);
            apoyo_[k - 1] = std::make_unique<SupportBandRow>(state, n, s, "A" + s);
            addAndMakeVisible(*apoyo_[k - 1]);
        }
    }

    void resized() override {
        auto bounds = getLocalBounds();
        activo.setBounds(bounds.removeFromTop(22).reduced(4, 0));
        arranque.setBounds(bounds.removeFromTop(24).reduced(4, 0));
        for (auto* row : { &intervalo, &ganancia, &mezclador, &velocidad, &extras })
            row->setBounds(bounds.removeFromTop(24).reduced(4, 0));
        for (auto& a : apoyo_)
            a->setBounds(bounds.removeFromTop(22).reduced(4, 0));
    }

private:
    ParamToggle activo;
    ParamCombo arranque;
    ParamRow intervalo, ganancia, mezclador, velocidad, extras;
    std::array<std::unique_ptr<SupportBandRow>, 4> apoyo_;
};

class AcousticalEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AcousticalEditor(AcousticalAudioProcessor& p)
        : AudioProcessorEditor(p), processor_(p),
          correccion_(p.parameters(), "correction", utf8("Corrección")),
          link_(p.parameters(), "link", "Link L/R"),
          ruido_(p.parameters(), "noiseSub", utf8("Sustracción de ruido")),
          maxGain_(p.parameters(), "maxGain", utf8("G. máxima")),
          suavizado_(p.parameters(), "smoothing", "Suavizado"),
          splObjetivo_(p.parameters(), "targetSpl", utf8("SPL objet.")),
          retardo_(p.parameters(), "delayMs", utf8("Retardo ms")),
          intervaloAnalisis_(p.parameters(), "analysisInterval", utf8("Análisis"),
                             {"25 ms", "50 ms", "100 ms", "200 ms", "500 ms"}),
          bandas_(p.parameters(), "bandCount", "Bandas",
                  {"8", "10", "16", "31 (1/3 oct)", "124 (Ultra)"}),
          fft_(p.parameters(), "fftSize", "FFT",
               {"512", "1024", "2048", "4096", "8192"}),
          generador_(p.parameters(), "genSource", "Generador",
                     {utf8("Apagado"), utf8("Ruido rosa"), utf8("Ruido blanco"),
                      "Barrido", "Seno"}),
          nivelGen_(p.parameters(), "genLevel", utf8("Nivel dB")),
          freqGen_(p.parameters(), "genFreq", utf8("Seno Hz")) {
        theme::apply(*this);

        addAndMakeVisible(correccion_);
        addAndMakeVisible(link_);
        addAndMakeVisible(ruido_);

        auto makeButton = [this](juce::TextButton& b, const juce::String& text,
                                 bool highlight, std::function<void()> onClick) {
            b.setButtonText(text);
            b.setColour(juce::TextButton::buttonColourId,
                        highlight ? theme::surfaceHi : theme::surface);
            b.setColour(juce::TextButton::textColourOffId,
                        highlight ? theme::textPrimary : theme::textDim);
            b.onClick = std::move(onClick);
            addAndMakeVisible(b);
        };
        makeButton(referencia_, utf8("Capturar referencia"), true,
                   [this] { processor_.captureReferenceFromInput(); });
        makeButton(borrarRef_, utf8("Borrar ref."), false,
                   [this] { processor_.clearReference(); });
        makeButton(capturarRuido_, utf8("Capturar ruido"), true,
                   [this] { processor_.startNoiseCapture(); });
        makeButton(cancelarRuido_, "Cancelar", false,
                   [this] { processor_.cancelNoiseCapture(); });
        makeButton(borrarRuido_, utf8("Borrar ruido"), false,
                   [this] { processor_.clearNoiseProfile(); });

        estado_.setFont(juce::Font(12.0f, juce::Font::bold));
        estado_.setColour(juce::Label::textColourId, theme::eq1Cyan);
        estado_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(estado_);

        for (auto* row : { &maxGain_, &suavizado_, &splObjetivo_, &retardo_,
                           &nivelGen_, &freqGen_ })
            addAndMakeVisible(*row);
        for (auto* combo : { &intervaloAnalisis_, &bandas_, &fft_, &generador_ })
            addAndMakeVisible(*combo);

        eqCanvas_ = std::make_unique<EqCanvas>([this](int band, float gain) {
            processor_.engine().setBandGain(band, gain);
        });
        addAndMakeVisible(*eqCanvas_);

        for (int i = 0; i < 3; ++i) {
            const juce::String n = juce::String(i + 1);
            DynamicEqCard::Snapshot snap;
            snap.title = utf8("Ecu dinámico ") + n;
            snap.directionLabel = i == 0 ? utf8("Donde más se necesita")
                                : i == 1 ? utf8("Empezando por los graves")
                                         : utf8("Empezando por los agudos");
            snap.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            cards_[i] = std::make_unique<DynamicEqCard>(snap);
            addAndMakeVisible(*cards_[i]);

            panels_[i] = std::make_unique<EqControlsPanel>(processor_.parameters(), n);
            addAndMakeVisible(*panels_[i]);
        }
        startTimerHz(15);
        setSize(1120, 880);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::background);
        g.fillRect(getLocalBounds());
    }

    void resized() override {
        auto bounds = getLocalBounds();

        auto top = bounds.removeFromTop(106).reduced(8, 3);
        auto rowA = top.removeFromTop(26);
        correccion_.setBounds(rowA.removeFromLeft(104));
        link_.setBounds(rowA.removeFromLeft(84));
        ruido_.setBounds(rowA.removeFromLeft(150));
        referencia_.setBounds(rowA.removeFromLeft(140).reduced(0, 2));
        borrarRef_.setBounds(rowA.removeFromLeft(96).reduced(0, 2));
        capturarRuido_.setBounds(rowA.removeFromLeft(120).reduced(0, 2));
        cancelarRuido_.setBounds(rowA.removeFromLeft(88).reduced(0, 2));
        borrarRuido_.setBounds(rowA.removeFromLeft(96).reduced(0, 2));

        auto rowB = top.removeFromTop(26);
        maxGain_.setBounds(rowB.removeFromLeft(250));
        suavizado_.setBounds(rowB.removeFromLeft(250));
        splObjetivo_.setBounds(rowB.removeFromLeft(250));
        retardo_.setBounds(rowB.removeFromLeft(250));

        auto rowC = top.removeFromTop(26);
        intervaloAnalisis_.setBounds(rowC.removeFromLeft(180));
        bandas_.setBounds(rowC.removeFromLeft(170));
        fft_.setBounds(rowC.removeFromLeft(140));
        generador_.setBounds(rowC.removeFromLeft(170));
        nivelGen_.setBounds(rowC.removeFromLeft(200));
        freqGen_.setBounds(rowC.removeFromLeft(220));

        auto rowD = top.removeFromTop(24);
        estado_.setBounds(rowD);

        // Fila inferior: tarjeta + panel de controles por ecu dinámico.
        auto bottom = bounds.removeFromBottom(376);
        const int w = getWidth() / 3;
        for (int i = 0; i < 3; ++i) {
            auto col = bottom.removeFromLeft(w).reduced(4, 2);
            panels_[i]->setBounds(col.removeFromBottom(242));
            cards_[i]->setBounds(col.removeFromBottom(128));
        }
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
            const juce::String n = juce::String(i + 1);
            DynamicEqCard::Snapshot card;
            card.title = utf8("Ecu dinámico ") + n;
            card.directionLabel = i == 0 ? utf8("Donde más se necesita")
                                : i == 1 ? utf8("Empezando por los graves")
                                         : utf8("Empezando por los agudos");
            card.accent = i == 0 ? theme::eq1Cyan : i == 1 ? theme::eq2Amber : theme::eq3Magenta;
            card.maxGainDb = processor_.parameters()
                                 .getRawParameterValue("eq" + n + "Gain")->load();
            card.enabled = processor_.parameters()
                               .getRawParameterValue("eq" + n + "Enabled")->load() > 0.5f;
            if (analysis.dynamicEqGainsL.size() > static_cast<size_t>(i))
                card.gains = analysis.dynamicEqGainsL[i];
            if (sweeps[i].bandIndex >= 0) {
                card.activeBand = sweeps[i].bandIndex;
                card.channelBadge = sweeps[i].channel;
                card.statusText = juce::String(sweeps[i].centerFreqHz, 0)
                    + utf8(" Hz · ")
                    + juce::String(sweeps[i].gainDb, 1, true)
                    + utf8(" dB · suavizado ")
                    + juce::String(sweeps[i].smoothingMs, 0)
                    + utf8(" ms");
            }
            cards_[i]->setSnapshot(card);
        }

        // Línea de estado: SPL, RT60 (reflexiones), referencia y perfil de ruido
        juce::String estado;
        estado << juce::String::formatted(
            "SPL %.1f dB (pico %.1f, medio %.1f)  |  RT60 %.2f s",
            analysis.spl, analysis.peakSpl, analysis.averageSpl,
            analysis.rt60Ms / 1000.0f);
        estado << (processor_.engine().isReferenceCaptured()
                       ? utf8("  |  Referencia capturada")
                       : utf8("  |  Sin referencia"));
        if (processor_.engine().isNoiseCapturing())
            estado << juce::String::formatted("  |  Capturando ruido %d %%",
                static_cast<int>(processor_.engine().noiseCaptureProgress() * 100.0f));
        else if (processor_.engine().hasNoiseProfile())
            estado << utf8("  |  Perfil de ruido activo");
        estado_.setText(estado, juce::dontSendNotification);
    }

    AcousticalAudioProcessor& processor_;
    ParamToggle correccion_, link_, ruido_;
    ParamRow maxGain_, suavizado_, splObjetivo_, retardo_, nivelGen_, freqGen_;
    ParamCombo intervaloAnalisis_, bandas_, fft_, generador_;
    juce::TextButton referencia_, borrarRef_, capturarRuido_, cancelarRuido_, borrarRuido_;
    juce::Label estado_{"estado", juce::String()};
    std::unique_ptr<EqCanvas> eqCanvas_;
    std::array<std::unique_ptr<DynamicEqCard>, 3> cards_;
    std::array<std::unique_ptr<EqControlsPanel>, 3> panels_;
};
