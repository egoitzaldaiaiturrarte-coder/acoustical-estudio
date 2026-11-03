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
#include "CrashFilter.h"

#if !JUCE_WINDOWS
#include <unistd.h>   // getpid()
#endif

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
        SetUnhandledExceptionFilter(crashfilter::filter);
#if defined(__SANITIZE_ADDRESS__)
        // Build ASan de diagnóstico: sus informes (stderr) van a
        // %AppData%\Acoustical\asan.log en vez de a la nada.
        crashfilter::redirectStderrToLog();
        startuplog::log(juce::String::fromUTF8(
            "build ASan activo: los informes de memoria iran a asan.log"));
#endif
#endif
        startuplog::log(juce::String::fromUTF8("inicializando (")
            + juce::SystemStats::getJUCEVersion() + juce::String::fromUTF8(")"));
        try {
            // Abrir el audio nada más arrancar: 2 entradas + 2 salidas.
            // Cadena v1.11.1 (antes: SOLO el estado guardado, forzando
            // SIEMPRE modo exclusivo — con una tarjeta WDM normal el modo
            // exclusivo no entra, la app arrancaba en zombie sin audio y el
            // único rastro era una línea en startup.log que nadie lee):
            //   1. el estado guardado, en modo COMPARTIDO (la 4ª arg de
            //      initialise es now false: no forzar exclusivo — en Windows
            //      el puente ASIO-JUCE abre la tarjeta por defecto en
            //      exclusivo, y si no puede entrar, falla; el modo
            //      compartido sí entra en la tarjeta de siempre).
            //   2. las tarjetas por defecto del SO.
            //   3. cada dispositivo ASIO uno a uno (la app tiene su propio
            //      puente ASIO: si el usuario tiene una interfaz, probarla).
            //   4. si TODO falla: ventana visible con botón Reintentar (no
            //      un zombie mudo; la ventana no mata la app: puede abrirse
            //      la tarjeta más tarde y se reintenta).
            //   Y al salir, el estado solo se persiste si HAY dispositivo
            //      abierto (v1.11.1): antes se guardaba el estado "zombi"
            //      y el próximo arranque repetía el mismo fallo.
            const auto saved = juce::parseXML(audioStateFile());
            const auto err = deviceManager.initialise(2, 2, saved.get(), false);
            if (err.isNotEmpty()) {
                startuplog::log(juce::String::fromUTF8(
                    "audio: fallo al abrir la tarjeta guardada ('") + err
                    + juce::String::fromUTF8("'), probando con la por defecto"));
                const auto err2 = deviceManager.initialiseWithDefaultDevices(2, 2);
                if (err2.isNotEmpty()) {
                    startuplog::log(juce::String::fromUTF8(
                        "audio: fallo con la por defecto ('") + err2
                        + juce::String::fromUTF8("'), probando los ASIO uno a uno"));
                    tryLastResortAudioDevices(err2);
                }
            }
            if (auto* dev = deviceManager.getCurrentAudioDevice())
                startuplog::log(juce::String::fromUTF8("audio: tarjeta '") + dev->getName()
                    + juce::String::fromUTF8("' a ")
                    + juce::String(static_cast<int>(dev->getCurrentSampleRate()))
                    + juce::String::fromUTF8(" Hz · buffer ")
                    + juce::String(dev->getCurrentBufferSizeSamples()));
            else
                startuplog::log(juce::String::fromUTF8(
                    "audio: NO hay ninguna tarjeta abierta (se mostrará una "
                    "ventana con botón Reintentar; la app sigue funcionando "
                    "sin señal)"));
            mainWindow_ = std::make_unique<MainWindow>(deviceManager, engine);
            startuplog::log(juce::String::fromUTF8(
                "ventana: creada y visible — si no la ves, busca en la barra de "
                "tareas o en otro monitor"));
            if (deviceManager.getCurrentAudioDevice() == nullptr)
                showNoAudioDeviceWindow();
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
        // v1.11.1: solo se persiste el estado de audio si HAY dispositivo
        // abierto: el estado "zombi" (sin dispositivo) guardado por la
        // versión anterior hacía que el próximo arrancao volviera a fallar
        // exactamente igual (estado que nunca se puede abrir).
        if (deviceManager.getCurrentAudioDevice() != nullptr) {
            if (auto state = deviceManager.createStateXml()) {
                audioStateFile().getParentDirectory().createDirectory();
                if (auto out = audioStateFile().createOutputStream())
                    state->writeTo(*out);
            }
        } else {
            startuplog::log(juce::String::fromUTF8(
                "audio: no se guarda el estado (no había dispositivo abierto)"));
        }
        engine.stop();
        mainWindow_ = nullptr;
        deviceManager.closeAudioDevice();
    }

    void systemRequestedQuit() override { quit(); }

private:
    // Paso 3 de la cadena de arranque (v1.11.1): si el estado guardado y la
    // tarjeta por defecto fallaron, intentar combinar las entradas y salidas
    // que el SO ve con el MISMO API que usa la matriz de ruteo de la app
    // (getDeviceNames + setAudioDeviceSetup — RoutingMatrix.h): así no
    // depende de ninguna firma de JUCE que cambie entre versiones.
    void tryLastResortAudioDevices(const juce::String& prevErr) {
        auto* typeObj = deviceManager.getCurrentDeviceTypeObject();
        if (typeObj == nullptr) {
            startuplog::log(juce::String::fromUTF8(
                "audio: no hay ningún tipo de dispositivo activo del que "
                "enumerar entradas (último error: '") + prevErr + "'")
                + juce::String::fromUTF8(")"));
            return;
        }
        const juce::StringArray ins = typeObj->getDeviceNames(true);
        const juce::StringArray outs = typeObj->getDeviceNames(false);
        if (ins.isEmpty()) {
            startuplog::log(juce::String::fromUTF8(
                "audio: no hay entradas disponibles que probar (último error: '")
                + prevErr + juce::String::fromUTF8("')"));
            return;
        }
        for (int i = 0; i < ins.size(); ++i) {
            // Candidatos de salida: el primero de la lista y el que lleve el
            // mismo nombre que la entrada (la pareja típica de una tarjeta).
            juce::StringArray outCands;
            if (!outs.isEmpty()) outCands.add(outs[0]);
            if (outs.contains(ins[i])) outCands.add(ins[i]);
            if (outCands.isEmpty()) outCands.add(ins[i]);
            for (const auto& outName : outCands) {
                auto setup = deviceManager.getAudioDeviceSetup();
                setup.inputDeviceName = ins[i];
                setup.outputDeviceName = outName;
                const juce::String e = deviceManager.setAudioDeviceSetup(setup, true);
                startuplog::log(juce::String::fromUTF8("audio: probando '") + ins[i]
                    + juce::String::fromUTF8(" -> '") + outName
                    + juce::String::fromUTF8("': ") + e);
                if (e.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
                    return;  // entró: fin de la cadena
            }
        }
        startuplog::log(juce::String::fromUTF8(
            "audio: ninguna combinación probada entró (último error: '") + prevErr
            + juce::String::fromUTF8("')"));
    }

    // Ventana visible (no un zombie mudo) cuando nada abrió. El botón
    // "Reintentar" re-ejecuta la cadena (la tarjeta puede conectarse o
    // activarse más tarde); el "Cerrar" la descarta sin matar la app.
    // (standalone: no tiene padre; se crea una vez, aquí, en el hilo de
    // mensajes, antes de que el motor reciba audio.)
    void showNoAudioDeviceWindow() {
        auto* w = new juce::AlertWindow(
            juce::String::fromUTF8("Acoustical Estudio: no hay audio"),
            juce::String::fromUTF8(
                "No se ha podido abrir ninguna tarjeta de audio.\n\n"
                "La app sigue funcionando (puedes ver el estado, los "
                "parámetros y los registros), pero el motor no tiene señal "
                "hasta que se abra una tarjeta.\n\n"
                "Si conectas o activas una tarjeta (o su driver ASIO), "
                "pulsa Reintentar.\n\n"
                "El detalle está en: " + startuplog::logFile().getFullPathName()),
            juce::String::fromUTF8("Reintentar")
            + juce::String::fromUTF8("\nCerrar"),
            juce::MessageBoxIconType::WarningIcon);
        w->enterModalState();
    }

    static juce::File audioStateFile() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("audio.xml");
    }

    juce::AudioDeviceManager deviceManager;
    acoustical::AcousticalEngine engine;
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(AcousticalApplication)
