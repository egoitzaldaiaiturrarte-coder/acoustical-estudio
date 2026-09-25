// RemoteAudioLink.h — Puente de audio Wi-Fi con los móviles emparejados.
//
// Dos funciones, sobre UDP (independiente del canal de control TCP 41041):
//
//  M2 (micrófono del móvil → PC): el PC escucha en el puerto 41044. Cada
//     móvil que el PC le pida ("mic_start") envía tramas de 10 ms (48 kHz,
//     mono, PCM16) con su micrófono; aquí se convierten a un anillo lock-free
//     por dispositivo y el callback de audio las mezcla como una entrada más
//     (con su ganancia propia, Ruteos > Entradas > Móviles).
//
//  M3 (PC → altavoces del móvil): la ruta de red del Hub llama a sendToPhone()
//     cada 10 ms con la señal ya procesada (fase/retardo/ganancia); el PC la
//     envía por UDP al puerto 41043 del móvil, que la reproduce por sus
//     altavoces (jitter buffer + resampleo a su tasa en el móvil).
//
// Trama de audio (ambos sentidos): cabecera de 8 bytes BIG-ENDIAN +
// PCM16 LITTLE-ENDIAN intercalado:
//   u16 seq    — número de secuencia (0…65535, vuelta)
//   u16 rate   — tasa de muestreo (el móvil manda siempre 48000)
//   u16 ch     — canales (1 = mono, 2 = estéreo)
//   u16 rsvd   — reservado (0)
//
// Control (PC → móvil, al puerto 41043, JSON UTF-8 — se reconoce porque el
// primer byte es '{'):
//   {"cmd":"mic_start","pcIp":"a.b.c.d","port":41044,"rate":48000,"code":"…"}
//   {"cmd":"mic_stop"}
// El campo "code" es el código de emparejamiento: el móvil no abre su
// micrófono para el PC sin un código válido.
#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

class RemoteAudioLink {
public:
    struct Device {
        juce::String ip;
        juce::String id;            // identificador estable que genera el móvil
        juce::String name;          // modelo (viene en la baliza)
        juce::int64 lastBeaconMs = 0;   // última baliza (descubrimiento)
        juce::int64 lastAudioMs = 0;    // última trama de micrófono recibida
        bool micOn = false;            // el PC ha pedido el micrófono al móvil
    };

    RemoteAudioLink() = default;
    ~RemoteAudioLink();
    RemoteAudioLink(const RemoteAudioLink&) = delete;
    RemoteAudioLink& operator=(const RemoteAudioLink&) = delete;

    /** Abre el oyente UDP (41044) y arranca el hilo de recepción. Re-entrable. */
    void start();
    void stop();

    /** Devuelve el código de emparejamiento al pedir el micrófono (lo pone PhoneLink). */
    void setCodeProvider(std::function<juce::String()> provider);

    // === Registro de móviles (viene de la baliza 41042) ===

    /** Baliza vista: crea o actualiza el móvil en el registro. */
    void noteBeacon(const juce::String& ip, const juce::String& id, const juce::String& name);

    /** Todos los móviles vistos (para la UI). */
    std::vector<Device> devices() const;
    /** Móviles con baliza o audio recientes (para la UI). */
    std::vector<Device> liveDevices(int maxAgeMs = 5000) const;
    const Device* deviceByIp(const juce::String& ip) const;
    /** ¿Tiene baliza o audio reciente? (para cerrar rutas cuando el móvil se va). */
    bool isLive(const juce::String& ip, int maxAgeMs = 5000) const;
    /** "Móvil: Nombre" (texto de la UI) → IP. Vacío si no existe. */
    juce::String findIpByName(const juce::String& name) const;
    /** Nombre de pantalla de un móvil (único: incluye la IP). */
    static juce::String displayName(const Device& d);

    // === M2: micrófono del móvil como entrada del PC ===

    /** Pide (on) o para (off) el micrófono del móvil. false si el móvil es desconocido. */
    bool setMicOn(const juce::String& ip, bool on);
    bool micOn(const juce::String& ip) const;
    /** Ganancia del micrófono remoto en la mezcla (0…2). La UI escribe, el audio lee. */
    void setMicGain(const juce::String& ip, float g);
    float micGain(const juce::String& ip) const;
    /** Nivel pico de la última trama en dB (-120 = silencio). */
    float micLevelDb(const juce::String& ip) const;
    /** IPs de los móviles con el micrófono pedido (para el callback de audio). */
    std::vector<juce::String> activeMicIps() const;

    /** Hilo de audio: lee n muestras mono (-1…1) del micrófono remoto.
     *  Siempre rellena el buffer (silencio si no hay señal). Resamplea de 48 kHz
     *  a la tasa del dispositivo con interpolación lineal (el móvil manda 48 kHz). */
    void pullMic(const juce::String& ip, float* dest, int n, double outRate);

    // === M3: audio del PC a los altavoces del móvil ===

    /** Envía n muestras estéreo (-1…1) a los altavoces del móvil (ruta de red del Hub).
     *  No hace nada si el móvil es desconocido. */
    void sendToPhone(const juce::String& ip, const float* L, const float* R, int n, double rate);

    /** IP de este PC en la red Wi-Fi (para decírsela al móvil). Vacío sin red. */
    static juce::String ownLanIp();

    static constexpr int MIC_PORT = 41044;    // el PC escucha aquí los microfonos
    static constexpr int PLAY_PORT = 41043;   // el móvil escucha aquí (audio + control)

private:
    // Anillo mono lock-free (un escritor: hilo de red; un lector: audio).
    // Igual de semántica que el del Hub: al llenarse, el escritor descarta
    // lo más antiguo (la lectura siempre toma lo último).
    struct Ring {
        static constexpr int CAP = 16384;     // potencia de 2, ~340 ms a 48 kHz
        float data[CAP] = {};
        std::atomic<std::int64_t> w{0}, r{0};

        void write(const float* s, int n) {
            std::int64_t wv = w.load(std::memory_order_relaxed);
            const std::int64_t rv = r.load(std::memory_order_relaxed);
            if (wv - rv + n > CAP) r.store(wv + n - CAP, std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
                data[static_cast<int>((wv + i) & (CAP - 1))] = s[i];
            w.store(wv + n, std::memory_order_release);
        }
        int buffered() const {
            return static_cast<int>(w.load(std::memory_order_acquire)
                                    - r.load(std::memory_order_acquire));
        }
        std::int64_t head() const { return w.load(std::memory_order_acquire); }
        float at(std::int64_t absIndex) const {
            return data[static_cast<int>(absIndex & (CAP - 1))];
        }
    };

    // Estado por dispositivo. Los punteros a State son ESTABLES (el mapa no
    // borra entradas: los IPs no se multiplican y así el hilo de audio nunca
    // usa un State que la UI esté destruyendo).
    struct State {
        Device dev;
        Ring ring;
        double micPos = 0.0;                  // fracción de resampleo (0…1)
        std::atomic<float> gain{1.0f};
        std::atomic<float> levelDb{-120.0f};
        std::atomic<unsigned short> seqTx{0};
    };

    State* findState(const juce::String& ip) const;
    State* getOrCreate(const juce::String& ip);
    void runReceive();
    void sendControl(const juce::String& ip, const juce::String& json);
    void sendMicStart(const juce::String& ip);
    void loadMicState();
    void saveMicState();
    static juce::File stateFile();

    // mutable: findState() es const (lo llaman los getters) pero devuelve un
    // puntero no const; todos los cambios de State pasan por mutex o atomics.
    mutable std::map<juce::String, State> states_;
    mutable std::mutex mtx_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::function<juce::String()> codeProvider_;

    // Socket de envío compartido (el oyente tiene el suyo propio en el hilo).
    // Lo usan: hilo de red (pong) y las rutas de red del Hub (sendToPhone).
    juce::DatagramSocket sendSock_;
    std::mutex sendLock_;

    // Móviles cuyo micrófono estaba activo al salir (se re-enciende solo al
    // reaparecer, para no perder la track si el Wi-Fi titubea).
    std::vector<juce::String> micOnPersisted_;
};
