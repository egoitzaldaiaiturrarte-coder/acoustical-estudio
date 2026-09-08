// PhoneLink.h — vigilante USB + sincronización y actualización con la app móvil.
// Detecta la conexión del móvil (adb), reenvía puertos, sincroniza perfiles y
// ajustes por JSON, y se auto-actualiza descargando el instalador de Windows
// desde el propio móvil (verificación SHA-256 antes de ejecutar nada).
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class PhoneLink : private juce::Timer {
public:
    enum class State { NoAdb, WaitingForPhone, Connected, SyncError };

    struct Listener {
        virtual ~Listener() = default;
        virtual void phoneStateChanged(State newState, const juce::String& info) = 0;
        virtual void syncReceived(const juce::var& payload) = 0;
    };

    // Lo que el móvil anuncia sobre la versión de Windows disponible
    struct WindowsUpdateInfo {
        juce::String version;     // "" = el móvil no lleva paquete
        juce::String sha256;
        juce::int64 sizeBytes = 0;
        bool hasPayload = false;
        juce::String url;
    };

    explicit PhoneLink(juce::File adbExecutable);
    ~PhoneLink() override;

    void startWatchdog();          // sondea `adb devices` cada 1 s
    void stopWatchdog();

    State state() const { return state_.load(); }
    juce::String deviceSerial() const { return deviceSerial_; }
    juce::String lastSyncInfo() const;

    // Sincronización: envía el estado actual y pide el del móvil
    bool pushSync(const juce::var& payload);
    bool requestSync();

    // Comprueba con el móvil si hay una versión nueva de Windows; si la hay,
    // la descarga por USB, verifica su SHA-256 y lanza el instalador (con UAC).
    void checkForUpdate();
    WindowsUpdateInfo lastUpdateInfo() const;

    std::function<void(const juce::var&)> onSync;

private:
    void timerCallback() override;
    void pollDevices();
    juce::String runAdb(const juce::String& args, int timeoutMs = 4000);
    bool connectAndExchange(const juce::var& send, juce::var& reply);
    void setStatus(const juce::String& s);

    // Actualización desde el móvil
    void runUpdateCheck();
    juce::String httpGet(const juce::String& path, juce::MemoryBlock& body, int timeoutMs = 3000);
    bool httpDownloadToFile(const juce::String& path, const juce::File& dest, juce::int64 maxBytes);
    juce::String installedVersion() const;
    static int compareVersions(const juce::String& a, const juce::String& b);
    bool launchInstaller(const juce::File& installer) const;

    juce::File adb_;
    std::atomic<State> state_{State::WaitingForPhone};
    juce::String deviceSerial_;
    juce::CriticalSection statusLock_;
    juce::String lastInfo_;
    bool driverAttempted_ = false;

    std::thread updateThread_;
    std::atomic<bool> updateRunning_{false};
    mutable std::mutex updateMutex_;
    WindowsUpdateInfo updateInfo_;

    static constexpr int SYNC_PORT = 41041;   // mismo puerto que la app Android
    static constexpr juce::int64 MAX_PAYLOAD_BYTES = 512ll * 1024 * 1024;
};

// Instala el driver ADB incluido (una vez) con pnputil. Devuelve true si el
// driver ya estaba o se registró bien.
bool installAdbDriverOnce(const juce::File& driverFolder);
