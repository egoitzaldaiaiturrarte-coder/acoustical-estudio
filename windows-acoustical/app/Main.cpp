// Main.cpp — entrada de Acoustical Estudio. Ventana principal con la consola
// (pestañas EQ / Ecuas / Ruteos / Ajustes / Análisis / Audio) y arranque del
// vigilante USB. IMPORTANTE: aquí se abre el dispositivo de audio (sin esto el
// motor no recibe ni una muestra — causa del "motor no funciona" de la v1.1.x).
//
// Diagnóstico: cada arranque deja su rastro en startup.log (ver StartupLog.h):
// hitos del proceso, del audio, de la consola y, si algo falla, un filtro de
// excepciones (solo Windows) anota el código de fallo antes de morir.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "ConsoleComponent.h"
#include "Theme.h"
#include "StartupLog.h"

#if !JUCE_WINDOWS
#include <unistd.h>   // getpid()
#endif

#ifdef JUCE_WINDOWS
// === Filtro de crash (solo Windows/MSVC) =====================================
// Si la app hace un error fatal (p. ej. 0xC0000005 = acceso a memoria),
// Windows muestra su diálogo de error y el proceso muere. Para poder ver DÓNDE
// murió, anotamos el código y la dirección en startup.log con escrituras C
// puras (a esa altura no se puede fiar de JUCE ni del montón).
#include <windows.h>
#include <shlobj.h>
#include <cwchar>

namespace {
void crashAppend(const wchar_t* line) {
    wchar_t base[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base) != S_OK)
        return;
    wchar_t full[MAX_PATH] = {};
    swprintf(full, MAX_PATH, L"%s\\Acoustical\\startup.log", base);
    HANDLE h = CreateFileW(full, FILE_APPEND_DATA | FILE_READ_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(wcslen(line) * 2), &written, nullptr);
    CloseHandle(h);
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[320] = {};
    swprintf(line, 256,
             L"%04d-%02d-%02d %02d:%02d:%02d  CRASH: codigo de Windows 0x%08X en "
             L"%p - la app se detuvo. Esta linea y las anteriores dicen hasta "
             L"donde llego el arranque\r\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             ep->ExceptionRecord->ExceptionCode,
             ep->ExceptionRecord->ExceptionAddress);
    crashAppend(line);
    return EXCEPTION_EXECUTE_HANDLER;   // → diálogo estándar de Windows
}
}  // namespace
#endif  // JUCE_WINDOWS

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
    AcousticalApplication() {
        // Primera línea del log: el proceso existe (aunque luego falle todo).
        juce::String pid;
#if JUCE_WINDOWS
        pid = juce::String(GetCurrentProcessId());
#else
        pid = juce::String(static_cast<int>(getpid()));
#endif
        startuplog::log(juce::String::fromUTF8("=== PROCESO INICIADO === ")
            + JUCE_APPLICATION_VERSION_STRING
            + juce::String::fromUTF8(" · ") + juce::SystemStats::getOperatingSystemName()
            + juce::String::fromUTF8(" · PID ") + pid);
    }

    const juce::String getApplicationName() override { return "Acoustical Estudio"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    // El usuario volvió a abrir la app (doble clic, acceso directo…) mientras
    // otra copia ya estaba corriendo: JUCE mata la copia nueva y avisa a la
    // vieja por aquí. Lo decimos con claridad (antes el mensaje en inglés
    // dejaba a la gente sin saber qué hacer) y lo dejamos en el log.
    void anotherInstanceStarted(const juce::String&) override {
        startuplog::log(juce::String::fromUTF8(
            "arranque duplicado: ya había una copia de Acoustical corriendo; "
            "la copia nueva se ha descartado"));
        // Ventana no modal (no hay bucles modales en esta config): se autodestruye
        // cuando la cierra el usuario.
        new juce::AlertWindow(
            juce::String::fromUTF8("Acoustical Estudio ya está abierto"),
            juce::String::fromUTF8(
                "Acoustical Estudio ya está abierto.\n\n"
                "Busca su ventana (puede estar minimizada en la barra de "
                "tareas o en otro monitor). Si no aparece, ábrela con "
                "\"Ctrl + Alt + Supr\" → Administrador de tareas, busca "
                "\"Acoustical Estudio.exe\" y termínala; luego vuelve a "
                "abrir la app."),
            juce::MessageBoxIconType::InfoIcon);
    }

    void initialise(const juce::String&) override {
#ifdef JUCE_WINDOWS
        SetUnhandledExceptionFilter(crashFilter);
#endif
        startuplog::log(juce::String::fromUTF8("inicializando (JUCE ")
            + juce::SystemStats::getJUCEVersion() + juce::String::fromUTF8(")"));
        try {
            // Abrir el audio nada más arrancar: 2 entradas + 2 salidas. Si hay
            // un estado guardado (última tarjeta usada) se restaura; si no, la
            // por defecto de Windows.
            const auto saved = juce::parseXML(audioStateFile());
            const auto err = deviceManager.initialise(2, 2, saved.get(), true);
            if (err.isNotEmpty()) {
                startuplog::log(juce::String::fromUTF8(
                    "audio: fallo al abrir la tarjeta guardada ('") + err
                    + juce::String::fromUTF8("'), probando con la por defecto"));
                deviceManager.initialiseWithDefaultDevices(2, 2);
            }
            if (auto* dev = deviceManager.getCurrentAudioDevice())
                startuplog::log(juce::String::fromUTF8("audio: tarjeta '") + dev->getName()
                    + juce::String::fromUTF8("' a ")
                    + juce::String(static_cast<int>(dev->getCurrentSampleRate()))
                    + juce::String::fromUTF8(" Hz · buffer ")
                    + juce::String(dev->getCurrentBufferSizeSamples()));
            else
                startuplog::log(juce::String::fromUTF8(
                    "audio: NO hay ninguna tarjeta abierta (la app sigue, pero "
                    "el motor no tendrá señal)"));
            mainWindow_ = std::make_unique<MainWindow>(deviceManager, engine);
            startuplog::log(juce::String::fromUTF8(
                "ventana: creada y visible — si no la ves, busca en la barra de "
                "tareas o en otro monitor"));
        } catch (const std::exception& e) {
            startuplog::log(juce::String::fromUTF8("EXCEPCIÓN C++: ") + e.what());
            new juce::AlertWindow(
                juce::String::fromUTF8("Acoustical Estudio no pudo arrancar"),
                juce::String::fromUTF8("Detalle: ") + e.what()
                    + juce::String::fromUTF8("\n\nEl error se ha anotado en: ")
                    + startuplog::logFile().getFullPathName(),
                juce::MessageBoxIconType::WarningIcon);
            quit();
        }
    }

    void shutdown() override {
        startuplog::log(juce::String::fromUTF8("=== SALIDA LIMPIA === (adiós)"));
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
