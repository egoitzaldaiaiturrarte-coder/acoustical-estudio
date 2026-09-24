// BeaconListener.h — Oyente de la baliza UDP que la app móvil emite por
// Wi-Fi (puerto 41042, paquete JSON {"app":"acoustical","port":41041,...}).
// Quien manda la baliza es el móvil; aquí solo se escucha, así que el PC no
// necesita abrir ningún puerto entrante salvo este. Corre en su propio hilo
// con timeouts cortos para poder pararlo sin bloquear nada.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class BeaconListener {
public:
    using Callback = std::function<void(const juce::String& ip, const juce::var& info)>;

    explicit BeaconListener(int port);
    ~BeaconListener();

    /** Arranca el hilo de escucha. (Si el puerto estuviera ocupado, el
     *  hilo se termina solo y isFresh() devuelve siempre false.) */
    void start(Callback cb);
    void stop();

    /** ¿Se ha oído la baliza en los últimos maxAgeMs? */
    bool isFresh(int maxAgeMs) const;
    juce::String lastIp() const;

private:
    void run();

    int port_;
    Callback cb_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mtx_;
    juce::String ip_;
    std::atomic<juce::int64> lastMs_{0};
};
