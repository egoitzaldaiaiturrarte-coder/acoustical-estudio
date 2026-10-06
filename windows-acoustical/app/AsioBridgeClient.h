// AsioBridgeClient.h — lado de la APP del puente ASIO (C5).
//
// El driver Acoustical Bridge (asio-bridge/) se ve para Cubase como una
// interfaz ASIO virtual. El audio viaja por un bloque de memoria compartida
// de nombre fijo (BridgeShared.h) con dos anillos:
//
//   capture ring   (driver escribe, la app lee): el audio de Cubase — lo que
//        el usuario rutea a las salidas ASIO "Bridge Out L/R". La app lo
//        resamplea a la tasa de la tarjeta real y lo alimenta al motor: pasa
//        por el análisis y por el EQ, y suena por la tarjeta real.
//   playback ring  (la app escribe, driver lee): la señal de prueba
//        (generador) o silencio; el driver la reproduce en las entradas
//        "Bridge In L/R" para que Cubase la oiga/grabe y se verifique el
//        camino.
//
// RELACIÓN CREADOR/LECTOR (protocolo compartido con el driver, frentes 4A/4B):
//  - NOMBRE: kSharedMemoryBase + "\{8 hex del MachineGuid de la máquina}"
//    (bridge::makeSharedName). La app reimplementa la lectura del MachineGuid
//    (readMachineGuid abajo): la MISMA receta que el driver, para que ambos
//    caigan en el MISMO objeto. Si no se puede leer el GUID, se cae a la base
//    sin sufijo y la validación cruzada de instanceId pilla la divergencia.
//  - CREADOR: el primer extremo que llega (magic inválido en la memoria)
//    inicializa el header campo a campo con initializeHeader() publicando SU
//    instanceId; el store release de `magic` al final es la publicación.
//    LECTOR: el otro extremo abre la sección con magic ya válida y solo lee el
//    header: no toca instanceId (es del creador) ni lo reinicializa.
//    El struct YA NO ES COPIABLE (contiene std::atomic): `*h = {}` no compila.
//  - VIVOS: el driver incrementa `tick` cada periodo de su worker; la app
//    incrementa `appTick` en cada pullCapture/pushPlayback. El driver congela
//    el playback (bit kStatusAppDead) si el appTick deja de avanzar ~5 periodos.
//    A la inversa, esta app expone driverAlive()/lastDriverActivityMs() (lee
//    `tick`) y peerSessionMismatch() (instanceId de la sección vs la valor
//    observada al conectar, tras un tiempo con magia válida): la UI los muestra
//    en la pestaña Audio.
//
// pullCapture/pushPlayback se llaman desde el callback de audio (hilo de
// audio de la app); connect/disconnect desde el hilo de UI. El único
// intercambio con el driver son los anillos e índices (sin locks entre
// procesos): el anillo es tan largo (~340 ms) que absorbe la deriva de
// relojes, y si la app se atrasa más del anillo se resincroniza a la cola.
#pragma once

#include "BridgeShared.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {
// MachineGuid (HKLM\SOFTWARE\Microsoft\Cryptography): el driver (frente 4A)
// lo lee con esta misma receta para el sufijo del nombre de la sección
// (BridgeShared.h, makeSharedName). La app debe caer en el MISMO objeto; si
// no se puede leer, se devuelve "" y el nombre es la base sin sufijo (la
// validación cruzada de instanceId pilla la divergencia).
inline std::wstring readMachineGuid() {
#ifdef _WIN32
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return L"";
    wchar_t buf[128] = {};
    DWORD size = sizeof(buf), type = 0;
    const LSTATUS st = RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type,
                                       reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(hKey);
    if (st != ERROR_SUCCESS || type != REG_SZ) return L"";
    return std::wstring(buf);   // null-terminado; el constructor copia hasta el \0
#else
    return L"";
#endif
}
}  // namespace

class AsioBridgeClient {
public:
    AsioBridgeClient() = default;
    ~AsioBridgeClient() { disconnect(); }

    AsioBridgeClient(const AsioBridgeClient&) = delete;
    AsioBridgeClient& operator=(const AsioBridgeClient&) = delete;

    // Conecta al bloque compartido. Si el driver aún no está activo, lo crea:
    // el driver se une por nombre y respeta el header si la magia ya es
    // válida, así que da igual quién llega primero.
    bool connect() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected_.load(std::memory_order_relaxed)) return true;
        // El nombre es base + sufijo de máquina (MISMA receta que el driver,
        // ver readMachineGuid): si no se pudo leer el GUID, la base sola.
        const std::wstring name = bridge::makeSharedName(readMachineGuid());
        HANDLE hMap = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!hMap)
            hMap = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(bridge::kSharedBlockSize),
                                        name.c_str());
        if (!hMap) return false;
        void* view = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0,
                                     bridge::kSharedBlockSize);
        if (!view) {
            ::CloseHandle(hMap);
            return false;
        }
        auto* header = static_cast<bridge::BridgeHeader*>(view);
        // Magic inválida = sección recién creada (o basura): soy el CREADOR y
        // inicializo el header campo a campo publicando mi instanceId. El
        // struct YA NO ES COPIABLE (tiene std::atomic), así que el viejo
        // `*header = BridgeHeader{}` no compila: se usa initializeHeader()
        // (magic se publica al final, con release). Con magic válida soy
        // LECTOR: no toco instanceId (pertenece al creador).
        const bool magicOk = (header->magic.load(std::memory_order_acquire) == bridge::kMagic);
        myInstanceId_ = makeInstanceId();
        if (!magicOk) {
            weAreCreator_ = true;
            bridge::initializeHeader(header, myInstanceId_);
        } else {
            weAreCreator_ = false;
        }
        // Lo que la sección decía al conectarme: mi instanceId (si soy el
        // creador) o el del creador (si soy lector). peerSessionMismatch()
        // compara contra esto tras un tiempo con magia válida.
        sectionInstanceIdAtConnect_ = header->instanceId;
        magicValidSinceMs_ = GetTickCount64();
        // Snapshot inicial del heartbeat del driver: si `tick` ya ha avanzado
        // (el worker va en marcha), la última actividad es ahora; si es 0, el
        // worker aún no partió y el driver "no está activo" hasta que avise.
        lastSeenTick_ = static_cast<long long>(header->tick);
        lastTickAtMs_ = (lastSeenTick_ > 0) ? GetTickCount64() : 0;
        handle_ = hMap;
        header_ = header;
        capPos_ = 0.0;
        pushFrac_ = 0.0;
        connected_.store(true, std::memory_order_relaxed);
        return true;
#else
        return false;
#endif
    }

    void disconnect() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mutex_);
        connected_.store(false, std::memory_order_relaxed);
        if (header_) {
            // UnmapViewOfFile recibe el puntero de la vista (no el handle).
            ::UnmapViewOfFile(header_);
            ::CloseHandle(handle_);
            handle_ = nullptr;
            header_ = nullptr;
        }
#else
        connected_.store(false, std::memory_order_relaxed);
#endif
    }

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

    // sampleRate/bufferSize son std::atomic<uint32_t> en el header (ya no se
    // leen "a pelo"): siempre .load()/.store() — relaxed basta (ver el bloque
    // de memoria de BridgeShared.h).
    uint32_t sampleRate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return header_ ? header_->sampleRate.load(std::memory_order_relaxed) : 0;
    }
    uint32_t bufferSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return header_ ? header_->bufferSize.load(std::memory_order_relaxed) : 0;
    }

    // === Vivos y sesión (los consume la UI; el driver los escribe) ===

    // ¿El worker del driver sigue avanzando `tick`? Falsa si el worker nunca
    // ha avisado (Cubase sin audio) o si el último aviso es mayor de ~5
    // periodos (mismo umbral que el driver usa con la app: 5×periodo, suelo
    // de 20 ms → 100 ms).
    bool driverAlive() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_) return false;
        refreshDriverSnapshotLocked();
        if (lastTickAtMs_ == 0) return false;
        const uint32_t sr = header_->sampleRate.load(std::memory_order_relaxed);
        const uint32_t bs = header_->bufferSize.load(std::memory_order_relaxed);
        uint64_t periodMs = (sr > 0)
            ? static_cast<uint64_t>(static_cast<double>(bs) * 1000.0 / static_cast<double>(sr) + 0.5)
            : 10;
        uint64_t thr = periodMs * 5;
        if (thr < 20) thr = 20;
        return (GetTickCount64() - lastTickAtMs_) <= thr;
    }

    // Milisegundos desde la última vez que `tick` avanzó (0 = nunca).
    uint64_t lastDriverActivityMs() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_) return 0;
        refreshDriverSnapshotLocked();
        if (lastTickAtMs_ == 0) return 0;
        return GetTickCount64() - lastTickAtMs_;
    }

    // ¿Alguien re-inicializó el header bajo mis pies? Tras un tiempo con magia
    // válida (~5 s), la instanceId de la sección debe seguir siendo la que
    // observé al conectarme (la mía si soy el creador — el caso normal de la
    // app, que suele arrancar antes que Cubase; la del creador si soy lector).
    // Si cambió, otro proceso tomó el papel y la sección que veo no es la que
    // creo (split-brain / otra sesión) → la UI puede avisar.
    bool peerSessionMismatch() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_) return false;
        if (header_->magic.load(std::memory_order_acquire) != bridge::kMagic) return false;
        if (GetTickCount64() - magicValidSinceMs_ < kSessionSettleMs) return false;
        return header_->instanceId != sectionInstanceIdAtConnect_;
    }

    // El driver detectó que la app dejó de avisar (appTick congelado) y
    // congeló el playback: estado visible desde el header.
    bool appDeadFlag() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_) return false;
        return (header_->statusFlags & bridge::kStatusAppDead) != 0;
    }

    // Extrae audio de Cubase (capture ring) resampleado a outSampleRate.
    // Devuelve los fotogramas escritos en outL/outR (mono por array, estéreo).
    // Solo lo toca el hilo de audio.
    int pullCapture(float* outL, float* outR, int maxOutFrames, double outSampleRate) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_ || outSampleRate <= 1.0 || maxOutFrames <= 0) return 0;
        bridge::BridgeHeader& h = *header_;
        // Heartbeat a al driver (frente 4A lo lee para saber que sigo vivo):
        // se toca en cada llamada, aunque no se produzcan frames.
        h.appTick += 1;
        if (h.sampleRate.load(std::memory_order_relaxed) == 0) return 0;
        refreshDriverSnapshotLocked();

        const uint64_t ringFrames = bridge::kRingFrames;
        const float* cap = bridge::captureRing(&h);
        const uint64_t w = h.captureWriteIndex;

        // La app estuvo parada más de lo que cabe en el anillo: se desbordó.
        // Resincroniza a la cola (descarta el audio viejo) y empieza de nuevo.
        if (w - static_cast<uint64_t>(capPos_) > ringFrames) {
            capPos_ = static_cast<double>(w);
            return 0;
        }

        // Interpolación lineal: por cada fotograma de salida consume
        // ringRate/outRate fotogramas del anillo.
        const double step = static_cast<double>(h.sampleRate.load(std::memory_order_relaxed)) / outSampleRate;
        int produced = 0;
        while (produced < maxOutFrames) {
            const uint64_t i0 = static_cast<uint64_t>(capPos_);
            if (i0 + 1 > w) break;  // aún no hay una muestra completa disponible
            const uint64_t a = i0 % ringFrames;
            const uint64_t b = (i0 + 1) % ringFrames;
            const double frac = capPos_ - static_cast<double>(i0);
            outL[produced] = static_cast<float>(
                cap[a * bridge::kChannels] * (1.0 - frac) + cap[b * bridge::kChannels] * frac);
            outR[produced] = static_cast<float>(
                cap[a * bridge::kChannels + 1] * (1.0 - frac) + cap[b * bridge::kChannels + 1] * frac);
            ++produced;
            capPos_ += step;
        }
        // Consume hasta la parte entera (siempre <= a lo realmente usado).
        h.captureReadIndex = static_cast<uint64_t>(capPos_);
        return produced;
    }

    // Envía audio a Cubase (playback ring) resampleado desde inSampleRate a
    // la tasa del anillo. Solo lo toca el hilo de audio.
    void pushPlayback(const float* inL, const float* inR, int inFrames, double inSampleRate) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_ || inFrames <= 0 || inSampleRate <= 1.0) return;
        bridge::BridgeHeader& h = *header_;
        // Heartbeat a al driver (ver pullCapture).
        h.appTick += 1;
        // FIX del underflow (espejo del fix del jitter buffer): si la llamada
        // anterior se cortó a mitad por anillo lleno, pushFrac_ quedó MUY
        // negativo; sin este guard, el guard de anillo-lleno (abajo) no se
        // activa cuando el driver ya consumió y `pos < 0` haría inL[i0] leer
        // ANTES del buffer (heap underflow). Se encierra a 0.
        if (pushFrac_ < 0.0) pushFrac_ = 0.0;
        if (h.sampleRate.load(std::memory_order_relaxed) == 0) return;
        refreshDriverSnapshotLocked();

        // Anillo lleno (el driver no consume, p. ej. Cubase parado): descarta
        // en lugar de pisar lo que no ha leído.
        const uint64_t rd = h.playbackReadIndex;
        uint64_t w = h.playbackWriteIndex;
        const uint64_t ringFrames = bridge::kRingFrames;
        if (w - rd >= ringFrames) {
            pushFrac_ = 0.0;
            return;
        }

        float* pb = bridge::playbackRing(&h);
        // Por cada fotograma del anillo produce, consume inRate/ringRate
        // fotogramas de entrada; pushFrac_ es la fracción pendiente del
        // siguiente buffer.
        const double step = inSampleRate / static_cast<double>(h.sampleRate.load(std::memory_order_relaxed));
        double pos = pushFrac_;
        while (pos + 1.0 <= static_cast<double>(inFrames) && w - rd < ringFrames) {
            const int i0 = static_cast<int>(pos);
            const double frac = pos - static_cast<double>(i0);
            // i0+1 llega a inFrames cuando pos == inFrames-1 (p. ej. siempre
            // que inRate == ringRate, step == 1.0): se aprisiona a la última
            // muestra válida (con frac 0 el valor no cambia; evita la lectura
            // fuera de bounds pillada por ASan: heap-buffer-overflow).
            const int i1 = (i0 + 1 < inFrames) ? i0 + 1 : i0;
            const uint64_t idx = w % ringFrames;
            pb[idx * bridge::kChannels] = static_cast<float>(
                inL[i0] * (1.0 - frac) + inL[i1] * frac);
            pb[idx * bridge::kChannels + 1] = static_cast<float>(
                inR[i0] * (1.0 - frac) + inR[i1] * frac);
            ++w;
            pos += step;
        }
        pushFrac_ = pos - static_cast<double>(inFrames);
        // Clamp al rango [0, inFrames]: si el bucle se cortó por anillo lleno,
        // `pos` queda por debajo de inFrames y la fracción "pendiente" sería
        // negativa; el guard de entrada la anularía, pero dejar el estado
        // siempre sano hace el invariante obvio.
        if (pushFrac_ < 0.0) pushFrac_ = 0.0;
        if (pushFrac_ > static_cast<double>(inFrames)) pushFrac_ = static_cast<double>(inFrames);
        h.playbackWriteIndex = w;
    }

private:
    // Mi identidad (la del extremo que crea la sección): misma receta que el
    // driver (frente 4A): GetTickCount64()*100000 + pid — única por proceso.
    static uint64_t makeInstanceId() {
#ifdef _WIN32
        return (GetTickCount64() * 100000ull) + static_cast<uint64_t>(GetCurrentProcessId());
#else
        return 0;
#endif
    }

    // ¿`tick` cambió desde mi última observación? (bajo mutex_; el hilo de
    // audio llama aquí vía pullCapture/pushPlayback y la UI vía los getters).
    void refreshDriverSnapshotLocked() {
        if (header_ == nullptr) return;
        // volatile long long alineado de 64 bits: carga atómica en x86/x64
        // (mismo tratamiento que el driver da a sus heartbeats).
        const long long t = header_->tick;
        if (t != lastSeenTick_) {
            lastSeenTick_ = t;
            lastTickAtMs_ = GetTickCount64();
        }
    }

    mutable std::mutex mutex_;
    std::atomic<bool> connected_{false};
    bridge::BridgeHeader* header_ = nullptr;
    double capPos_ = 0.0;    // resampler de captura: posición fraccional en el anillo
    double pushFrac_ = 0.0;  // resampler de playback: fracción para el próximo buffer
    // Identidad y vivos (protocolo creador/lector, ver cabecera)
    uint64_t myInstanceId_ = 0;
    bool weAreCreator_ = false;
    uint64_t sectionInstanceIdAtConnect_ = 0;
    uint64_t magicValidSinceMs_ = 0;   // GetTickCount64 cuando la magia pasó a válida
    uint64_t lastSeenTick_ = 0;        // último `tick` del driver que observé
    uint64_t lastTickAtMs_ = 0;        // 0 = el worker nunca ha avanzado tick
    static constexpr uint64_t kSessionSettleMs = 5000;  // "tras un tiempo de magia válida"
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#endif
};
