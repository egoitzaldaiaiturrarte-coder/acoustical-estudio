// PhoneLink.h — vigilante USB + sincronización con la app Acoustical del móvil.
// Detecta la conexión del móvil (adb), reenvía puertos y sincroniza perfiles,
// correcciones, geometría y ajustes por JSON sobre TCP.
#pragma once

#include <juce_core/juce_core.h>
#include <functional>

class PhoneLink : private juce::Timer {
public:
    enum class State { NoAdb, WaitingForPhone, Connected, SyncError };

    struct Listener {
        virtual ~Listener() = default;
        virtual void phoneStateChanged(State newState, const juce::String& info) = 0;
        virtual void syncReceived(const juce::var& payload) = 0;
    };

    explicit PhoneLink(juce::File adbExecutable);
    ~PhoneLink() override { stopWatchdog(); }

    void startWatchdog();          // sondea `adb devices` cada 2 s
    void stopWatchdog();

    State state() const { return state_.load(); }
    juce::String deviceSerial() const { return deviceSerial_; }
    juce::String lastSyncInfo() const { return lastInfo_; }

    // Sincronización: envía el estado actual y pide el del móvil
    bool pushSync(const juce::var& payload);
    bool requestSync();

    std::function<void(const juce::var&)> onSync;

private:
    void timerCallback() override;
    void pollDevices();
    juce::String runAdb(const juce::String& args, int timeoutMs = 4000);
    bool connectAndExchange(const juce::var& send, juce::var& reply);

    juce::File adb_;
    std::atomic<State> state_{State::WaitingForPhone};
    juce::String deviceSerial_;
    juce::String lastInfo_;
    bool driverAttempted_ = false;

    static constexpr int SYNC_PORT = 41041;   // mismo puerto que la app Android
};

// Instala el driver ADB incluido (una vez) con pnputil. Devuelve true si el
// driver ya estaba o se registró bien.
bool installAdbDriverOnce(const juce::File& driverFolder);
