// Main.cpp — entrada de Acoustical Estudio. Ventana principal con la consola
// (pestañas EQ / Ruteos / Ajustes / Análisis) y arranque del vigilante USB.
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
        setResizeLimits(1120, 600, 8192, 8192);
        centreWithSize(1280, 800);
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
    const juce::String getApplicationVersion() override { return "1.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        mainWindow_ = std::make_unique<MainWindow>(deviceManager, engine);
    }

    void shutdown() override {
        engine.stop();
        mainWindow_ = nullptr;
        deviceManager.closeAudioDevice();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::AudioDeviceManager deviceManager;
    acoustical::AcousticalEngine engine;
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(AcousticalApplication)
