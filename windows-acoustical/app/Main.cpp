// Main.cpp — entrada de Acoustical Estudio. Ventana principal con la consola
// (pestañas EQ / Ecuas / Ruteos / Ajustes / Análisis / Audio) y arranque del
// vigilante USB. IMPORTANTE: aquí se abre el dispositivo de audio (sin esto el
// motor no recibe ni una muestra — causa del "motor no funciona" de la v1.1.x).
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "ConsoleComponent.h"
#include "Theme.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(juce::AudioDeviceManager& dm, acoustical::AcousticalEngine& engine)
        : DocumentWindow("Acoustical Estudio", theme::background,
                         juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        console_ = std::make_unique<ConsoleComponent>(dm, engine);
        setContentNonOwned(console_.get(), false);
        setResizable(true, false);
        setResizeLimits(1280, 600, 8192, 8192);
        centreWithSize(1380, 820);
        setVisible(true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    std::unique_ptr<ConsoleComponent> console_;
};

class AcousticalApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Acoustical Estudio"; }
    const juce::String getApplicationVersion() override { return "1.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        // Abrir el audio nada más arrancar: 2 entradas + 2 salidas. Si hay un
        // estado guardado (última tarjeta usada) se restaura; si no, la por
        // defecto de Windows.
        const auto saved = juce::parseXML(audioStateFile());
        const auto err = deviceManager.initialise(2, 2, saved.get(), true);
        if (err.isNotEmpty())
            deviceManager.initialiseWithDefaultDevices(2, 2);
        mainWindow_ = std::make_unique<MainWindow>(deviceManager, engine);
    }

    void shutdown() override {
        if (auto state = deviceManager.createStateXml()) {
            audioStateFile().getParentDirectory().createDirectory();
            if (auto out = audioStateFile().createOutputStream())
                state->writeTo(*out);
        }
        engine.stop();
        mainWindow_ = nullptr;
        deviceManager.closeAudioDevice();
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File audioStateFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("audio.xml");
    }

    juce::AudioDeviceManager deviceManager;
    acoustical::AcousticalEngine engine;
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(AcousticalApplication)
