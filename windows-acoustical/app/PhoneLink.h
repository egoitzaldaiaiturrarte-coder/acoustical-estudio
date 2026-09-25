// PhoneLink.h — Enlace con la app móvil. Tres caminos, en orden de
// preferencia:  1) Wi-Fi directo (la app del móvil emite una baliza UDP y
// el PC se conecta a su IP por TCP);  2) IP del móvil puesta a mano
// (routers que bloquean la baliza);  3) USB por adb (respaldo, como antes).
// Sincroniza perfiles y ajustes por JSON y auto-actualiza el PC descargando
// el instalador desde el móvil (verificación SHA-256 antes de ejecutar nada).
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "BeaconListener.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

class PhoneLink : private juce::Timer {
public:
    enum class State { NoAdb, WaitingForPhone, Connected, SyncError };

    // Por qué está (o no) conectado el móvil
    enum class Transport { None, WiFi, ManualIp, USB };

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

    void startWatchdog();          // sondea cada 1 s: baliza Wi-Fi → IP manual → adb
    void stopWatchdog();

    State state() const { return state_.load(); }
    Transport transport() const;
    juce::String deviceSerial() const;   // USB: serial adb; Wi-Fi/manual: IP del móvil
    juce::String lastSyncInfo() const;

    // Sincronización: envía el estado actual y pide el del móvil.
    // Con replyOut != nullptr la respuesta la gestiona el llamador (no onSync).
    bool pushSync(const juce::var& payload, const juce::String& statusMsg, juce::var* replyOut = nullptr);
    bool requestSync(juce::var* replyOut = nullptr);
    void setStatus(const juce::String& s);

    // Comprueba con el móvil si hay una versión nueva de Windows; si la hay,
    // la descarga (Wi-Fi o USB), verifica su SHA-256 y lanza el instalador.
    void checkForUpdate();
    WindowsUpdateInfo lastUpdateInfo() const;

    // Emparejamiento Wi-Fi: el código de 6 dígitos que muestra la app del
    // móvil. Persistido en el archivo de configuración de la app.
    juce::String pairCode() const;
    void setPairCode(const juce::String& code);
    // IP del móvil a mano (opcional, avanzado): se usa si no se oye la baliza.
    juce::String manualPhoneIp() const;
    void setManualPhoneIp(const juce::String& ip);

    std::function<void(const juce::var&)> onSync;

private:
    void timerCallback() override;
    void pollDevices();
    void decideEndpoint();
    juce::String runAdb(const juce::String& args, int timeoutMs = 4000);
    // true si el transporte destino cambió (y por tanto hay que (re)conectarse)
    bool endpointChanged(const juce::String& host, Transport t);
    bool exchange(const juce::var& send, juce::var& reply);

    juce::File configPath() const;
    void loadConfig();
    void saveConfig();

    // Actualización desde el móvil
    void runUpdateCheck();
    juce::String httpGet(const juce::String& path, juce::MemoryBlock& body, int timeoutMs = 3000);
    bool httpDownloadToFile(const juce::String& path, const juce::File& dest, juce::int64 maxBytes);
    juce::String installedVersion() const;
    static int compareVersions(const juce::String& a, const juce::String& b);
    bool launchInstaller(const juce::File& installer) const;

    juce::File adb_;
    std::unique_ptr<BeaconListener> beacon_;
    std::atomic<State> state_{State::WaitingForPhone};
    juce::String deviceSerial_;
    juce::CriticalSection statusLock_;
    juce::String lastInfo_;
    int syncPollCounter_ = 0;   // sondeo de sincronización cada ~3 s si hay móvil

    juce::CriticalSection endpointLock_;
    juce::String endpointHost_;   // IP Wi-Fi del móvil, IP manual o 127.0.0.1 (USB)
    Transport transport_{Transport::None};

    // Configuración que la UI escribe y los hilos de red leen (copia protegida)
    juce::CriticalSection configLock_;
    juce::String pairCode_;
    juce::String manualIp_;

    std::thread updateThread_;
    std::atomic<bool> updateRunning_{false};
    mutable std::mutex updateMutex_;
    WindowsUpdateInfo updateInfo_;

    static constexpr int SYNC_PORT = 41041;    // mismo puerto que la app Android
    static constexpr int BEACON_PORT = 41042;  // baliza UDP del móvil por Wi-Fi
    static constexpr int BEACON_TTL_MS = 5000; // sin baliza en 5 s → otro camino
    static constexpr juce::int64 MAX_PAYLOAD_BYTES = 512ll * 1024 * 1024;
};

// Instala el driver ADB incluido (una vez) con pnputil. Devuelve true si el
// driver ya estaba o se registró bien. (Solo importa para el camino USB.)
bool installAdbDriverOnce(const juce::File& driverFolder);
