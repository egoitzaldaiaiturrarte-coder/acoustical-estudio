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

    /** Puerta de emparejamiento: solo se acepta una baliza cuyo campo "code"
     *  pase este validador. El oyente es agnóstico (no sabe nada de
     *  emparejamiento): lo pone PhoneLink, que compara el código de la baliza
     *  con el que el usuario pegó en Ajustes > Móvil.
     *  Por qué importa: antes la baliza solo comprobaba app=="acoustical" y el
     *  PC usaba la IP del emisor para la comprobación de actualización —cualquier
     *  dispositivo de la LAN podía emitir una baliza y el PC le apuntaba la
     *  descarga del /manifest//payload (RCE: ejecutaría el instalador que ese
     *  emisor sirviera). Con la puerta, una baliza sin el código emparejado no
     *  mueve el endpoint ni dispara nada.
     *  Si no se pone validador se acepta cualquier baliza (comportamiento
     *  antiguo); PhoneLink siempre pone uno. */
    void setCodeValidator(std::function<bool(const juce::String&)> validator);

private:
    void run();

    int port_;
    Callback cb_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mtx_;
    juce::String ip_;
    std::atomic<juce::int64> lastMs_{0};
    // El validador se pone desde el hilo de UI y se lee desde el hilo oyente:
    // el mutex protege el intercambio (el lector copia el std::function local
    // bajo el mismo lock, así que no se pisa ni se lee a medio escribir).
    mutable std::mutex validatorLock_;
    std::function<bool(const juce::String&)> codeValidator_;
};
