// InstanceLink.h — v1.11.0 (lote 2): la referencia entre instancias VST.
//
// Cada instancia de "Acoustical Dynamic EQ" cargada en el DAW:
//   - PUBLICA su salida post-fader (el mono que sale del plugin, ya con la
//     ganancia espacial) en un anillo SPSC de memoria compartida NOMBRADA
//     ("AcousticalVstRef-<id>"), derivada de su número de instancia de 4
//     cifras. Mismo patrón de anillo que el pre-EQ del procesador y el
//     RemoteAudioLink de la app: w/r atómicos, capacidad potencia de 2, el
//     escritor descarta lo más antiguo al llenarse.
//   - EMITE una baliza UDP local cada medio segundo (puerto 41046, el
//     patrón de la baliza 41042 del móvil: JSON con el campo "app":
//     {"app":"acoustical","vst":true,"id":"NNNN"}), a la multidifusión y al
//     loopback, para que el resto de instancias la descubra. En Windows,
//     SO_REUSEADDR deja que TODAS las instancias se binden al mismo puerto y
//     reciban todas las balizas (cada una ignora la suya por el id). En la
//     CI de Linux (donde un solo socket recibe las datagramas) los tests
//     inyectan el par con notePeerForTest(); el camino real de socket se
//     compila y corre en Windows.
//
// Si el usuario elige otra instancia como FUENTE DE REFERENCIA (parámetro
// refMaster; el combo de la ventana la lista), la captura de referencia
// (captureReferenceFromInput) lee la ventana más reciente del anillo del
// master en vez de su propio anillo pre-EQ. Si el master se cierra (su
// mapping deja de existir) o su baliza caduca, la captura cae a la entrada
// propia — el comportamiento de siempre.
//
// Reglas de hilo (igual que el resto del plugin):
//   - publish() lo llama SOLO el hilo de audio, sobre el anillo PROPIO.
//   - readMaster() lo llama el hilo de mensajes (la captura de referencia):
//     abre el mapping del master SOLO LECTURA y copia la ventana; no toca
//     nada del master.
//   - La baliza corre en su propio std::thread (sin bucle de mensajes de
//     JUCE: el plugin vive dentro del host y no puede pedirle modales).
//
// Nota de robustez: el lector abre el mapping por nombre; si el proceso del
// master muere, en POSIX el archivo sigue mapeado (leído con seguridad) y en
// Windows la section se suelta al cerrarse sus handles. La guarda práctica es
// la baliza: readMaster() exige que la baliza del master sea fresca (<=3 s),
// y el master deja de emitirla en el mismo instante en que deja de procesar.
#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
#  if !defined(WIN32_LEAN_AND_MEAN)
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif
#if JUCE_LINUX
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#endif

namespace instlink {

// Cabecera del anillo compartido (tamaño fijo, 64 bytes).
// Los campos w/r son std::atomic<int64>: lock-free en x86 de 32 y 64 bits
// (cmpxchg64 / interlocked), así el SPSC es seguro cruzando procesos (32/64).
constexpr std::uint32_t kMagic      = 0x31564341;  // "ACV1"
constexpr std::uint32_t kVersion    = 1;
constexpr int            kCap        = 32768;     // potencia de 2, ~0,7 s a 48 kHz
constexpr int            kHeaderSize = 64;        // bytes (la data empieza aquí)

struct RingHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t id;
    std::uint32_t rate;
    std::uint32_t ch;
    std::uint32_t cap;
    std::uint32_t rsvd;
    std::atomic<std::int64_t> w;
    std::atomic<std::int64_t> r;
    std::uint32_t pad[4];
    // La data (float mono) sigue inmediatamente después: cap * sizeof(float).
};
static_assert(sizeof(RingHeader) == kHeaderSize, "el header debe medir 64 bytes");

}  // namespace instlink

class InstanceLink {
public:
    struct Peer {
        juce::String id;            // número de instancia de 4 cifras
        juce::int64 lastBeaconMs = 0;
    };

    static constexpr int kBeaconPort = 41046;

    InstanceLink() = default;
    ~InstanceLink() { stop(); }
    InstanceLink(const InstanceLink&) = delete;
    InstanceLink& operator=(const InstanceLink&) = delete;

    /** Número de instancia (4 cifras). Lo genera el procesador y lo guarda
     *  en su estado: estable entre recargas del proyecto. */
    int id() const { return id_.load(std::memory_order_relaxed); }

    /** Arranca el anillo propio + la baliza. (Idempotente.) */
    void start(int id);
    /** Para la baliza y suelta el mapping propio (el destructor lo hace). */
    void stop();
    /** El estado restaurado trajo otro número: el anillo propio cambia de
     *  nombre al nuevo (los lectores se re-ubican por el id). */
    void reid(int newId);

    /** (Hilo de audio, sobre el anillo PROPIO) publica la salida post-fader. */
    void publish(const float* mono, int n, double rate);

    /** (Hilo de mensajes) pares con baliza vista en los últimos maxAgeMs. */
    std::vector<Peer> peers(int maxBeaconAgeMs) const;
    bool isPeerFresh(const juce::String& id, int maxBeaconAgeMs) const;

    /** (Hilo de mensajes) Ventana más reciente del anillo del master:
     *  true si el master está en el registro con baliza fresca Y su mapping
     *  se puede abrir (el master vivo tiene el mapping); out = las muestras,
     *  rate = la tasa a la que el master publicó. */
    bool readMaster(const juce::String& id, int maxBeaconAgeMs,
                    std::vector<float>& out, int& rate) const;

    // (Solo tests de CI, donde el socket de baliza no entrega entre procesos):
    void notePeerForTest(const juce::String& id);
    void clearPeersForTest();

private:
    void run();
    void sendBeacon();
    void upsertPeer(const juce::String& id);
    void pruneLocked();

    static std::int64_t nowMs() { return juce::Time::getMillisecondCounter(); }
    static void ensureWinsock();
    juce::String ringNameFor(const juce::String& id) const;
    void* openRing(const juce::String& name, std::size_t& size) const;
    void* createRing(const juce::String& name, std::size_t& size);
    void closeRing(void* map, std::size_t size) const;

    std::atomic<int> id_{0};

    // Mapping del anillo PROPIO (el dueño escribe). nullptr si no se ha
    // arrancado o no se pudo crear (en ese caso publish() no hace nada).
    // En Windows el handle del mapping NO se cierra en stop(): la section
    // debe vivir hasta que muera el proceso del master (es lo que define
    // "el master se cerró"); un handle con vida de proceso no es un leak
    // relevante (mismo criterio que WSAStartup, que tampoco se limpia).
    // En POSIX se hace shm_unlink al parar (el mapping de los lectores vivos
    // sigue válido: el archivo tmpfs no se destruye hasta soltar todos los
    // mappings, no solo el último handle).
    mutable std::mutex mapMtx_;
    void* map_ = nullptr;
    std::size_t mapSize_ = 0;

    std::atomic<bool> running_{false};
    std::thread thread_;
    int sock_ = -1;

    mutable std::mutex peersMtx_;
    std::map<juce::String, Peer> peers_;
    juce::int64 lastPruneMs_ = 0;

    static constexpr juce::int64 kStaleMs = 60 * 60 * 1000;  // 1 h sin baliza → se poda
};

// ============================================================================
// Implementación
// ============================================================================

#if JUCE_WINDOWS
namespace {
inline void ensureWinsockOnce() {
    // WSAStartup referencia (el dll del plugin se descarga, el proceso del
    // DAW vive: nunca se llama WSACleanup).
    static std::once_flag of;
    std::call_once(of, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
}
}
inline void InstanceLink::ensureWinsock() { ensureWinsockOnce(); }
#else
inline void InstanceLink::ensureWinsock() {}
#endif

inline juce::String InstanceLink::ringNameFor(const juce::String& id) const {
#if JUCE_LINUX
    return "/" + juce::String("AcousticalVstRef-") + id;
#else
    return juce::String("AcousticalVstRef-") + id;
#endif
}

inline void InstanceLink::closeRing(void* map, std::size_t size) const {
    if (map == nullptr) return;
#if JUCE_WINDOWS
    // (Solo munmap: ver la nota del miembro map_ — el handle no se cierra.)
    UnmapViewOfFile(map);
#else
    if (size > 0) munmap(map, size);
#endif
}

inline void* InstanceLink::openRing(const juce::String& name, std::size_t& size) const {
    size = 0;
#if JUCE_WINDOWS
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name.toWideCharNative());
    if (h == nullptr) return nullptr;
    auto* m = (void*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(h);
    return m;  // (el tamaño no importa: se valida el header al leer)
#else
    int fd = shm_open(name.toRawUTF8(), O_RDONLY, 0);
    if (fd < 0) return nullptr;
    struct stat st {};
    if (fstat(fd, &st) != 0) { close(fd); return nullptr; }
    auto* m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return nullptr;
    size = (size_t)st.st_size;
    return m;
#endif
}

inline void* InstanceLink::createRing(const juce::String& name, std::size_t& size) {
    // nullptr = no se pudo crear o YA existe (colisión de id: el llamador
    // reid() y reintenta).
    size = 0;
    const std::size_t total = (std::size_t)instlink::kHeaderSize
                            + (std::size_t)instlink::kCap * sizeof(float);
#if JUCE_WINDOWS
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  (DWORD)(total >> 32), (DWORD)(total & 0xFFFFFFFF),
                                  name.toWideCharNative());
    if (h == nullptr) return nullptr;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(h); return nullptr; }
    auto* m = (void*)MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, total);
    // (Ver la nota del miembro map_: el handle vive hasta que muere el proceso.)
    if (m == nullptr) return nullptr;
#else
    int fd = shm_open(name.toRawUTF8(), O_RDWR | O_CREAT | O_EXCL, 0600);
    // (O_RDWR es OBLIGATORIO en Linux: sin él el fd queda solo-lectura y
    //  ftruncate/mmap fallan — POSIX no lo documenta, la implementación sí.)
    if (fd < 0) return nullptr;  // (EEXIST = colisión)
    if (ftruncate(fd, (off_t)total) != 0) { close(fd); shm_unlink(name.toRawUTF8()); return nullptr; }
    auto* m = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { shm_unlink(name.toRawUTF8()); return nullptr; }
#endif
    auto* hdr = (instlink::RingHeader*)m;
    std::memset(hdr, 0, instlink::kHeaderSize);
    hdr->magic   = instlink::kMagic;
    hdr->version = instlink::kVersion;
    hdr->id      = (std::uint32_t)id_.load(std::memory_order_relaxed);
    hdr->rate    = 0;
    hdr->ch      = 1;
    hdr->cap     = (std::uint32_t)instlink::kCap;
    hdr->w.store(0, std::memory_order_relaxed);
    hdr->r.store(0, std::memory_order_relaxed);
    // La data no se zeroea entera (128 KB): el writer la rellena en uso y el
    // reader solo lee desde su posición r (que el writer mantiene <= w).
    size = total;
    return m;
}

inline void InstanceLink::start(int id) {
    id_.store(id, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mapMtx_);
        if (map_ == nullptr) {
            for (int t = 0; t < 8; ++t) {
                const juce::String name = ringNameFor(juce::String(id_.load()));
                std::size_t size = 0;
                void* m = createRing(name, size);
                if (m != nullptr) {
                    map_ = m;
                    mapSize_ = size;
                    break;
                }
                // Colisión de id (otra instancia con el mismo número): se
                // cambia a uno nuevo y se reintenta (el editor lo verá).
                id_.store(1000 + (int)(std::rand() % 9000), std::memory_order_relaxed);
            }
        }
    }
    if (running_.exchange(true)) return;  // ya arrancado
    ensureWinsock();
    sock_ = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ >= 0) {
        int one = 1;
#if JUCE_WINDOWS
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
        setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);
#else
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        sockaddr_in a {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((u_int16_t)kBeaconPort);
        bind(sock_, (sockaddr*)&a, sizeof a);
    }
    thread_ = std::thread([this] { run(); });
}

inline void InstanceLink::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) {
#if JUCE_WINDOWS
        closesocket(sock_);
#else
        close(sock_);
#endif
        sock_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(mapMtx_);
        if (map_ != nullptr) {
#if JUCE_LINUX
            munmap(map_, mapSize_);
            shm_unlink(ringNameFor(juce::String(id_.load())).toRawUTF8());
#endif
#if JUCE_WINDOWS
            UnmapViewOfFile(map_);  // (el handle no se cierra: ver la nota)
#endif
            map_ = nullptr;
        }
    }
}

inline void InstanceLink::reid(int newId) {
    if (newId < 1000 || newId > 9999) return;
    // El nombre del anillo viejo se captura ANTES de cambiar id_: lo que se
    // suelta es el mapping EN USO; el archivo nuevo se crea más abajo.
    // (Unlinkar el nombre nuevo aquí lo dejaría huérfano en /dev/shm.)
    const juce::String oldName = ringNameFor(juce::String(id_.load(std::memory_order_relaxed)));
    id_.store(newId, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mapMtx_);
    if (map_ != nullptr) {
#if JUCE_LINUX
        munmap(map_, mapSize_);
        shm_unlink(oldName.toRawUTF8());
#endif
#if JUCE_WINDOWS
        UnmapViewOfFile(map_);
#endif
        map_ = nullptr;
    }
    std::size_t size = 0;
    void* m = createRing(ringNameFor(juce::String(newId)), size);
    if (m != nullptr) {
        map_ = m;
        mapSize_ = size;
    }
}

inline void InstanceLink::publish(const float* mono, int n, double rate) {
    void* m = nullptr;
    {
        std::lock_guard<std::mutex> lock(mapMtx_);
        m = map_;
    }
    if (m == nullptr || n <= 0) return;
    auto* hdr = (instlink::RingHeader*)m;
    if (hdr->magic != instlink::kMagic) return;
    const auto cap = (std::int64_t)hdr->cap;
    auto* data = (float*)((char*)m + instlink::kHeaderSize);
    // Mismo esquema que el anillo pre-EQ (PreRing::write): w se avanza con
    // release tras escribir los datos; si el anillo se llena se descarta lo
    // más antiguo (el reader nunca escribe r: solo lee desde [r, w)).
    const std::int64_t wv = hdr->w.load(std::memory_order_relaxed);
    const std::int64_t rv = hdr->r.load(std::memory_order_relaxed);
    if (wv - rv + n > cap)
        hdr->r.store(std::max(rv, wv + n - cap), std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        data[static_cast<int>((wv + i) & (cap - 1))] = mono[i];
    hdr->rate = (std::uint32_t)rate;  // (escritura de 32 bits alineada en x86)
    hdr->w.store(wv + n, std::memory_order_release);
}

inline std::vector<InstanceLink::Peer> InstanceLink::peers(int maxBeaconAgeMs) const {
    std::lock_guard<std::mutex> lock(peersMtx_);
    std::vector<Peer> out;
    const std::int64_t now = nowMs();
    for (const auto& [k, p] : peers_)
        if (p.lastBeaconMs > 0 && now - p.lastBeaconMs <= maxBeaconAgeMs) out.push_back(p);
    return out;
}

inline bool InstanceLink::isPeerFresh(const juce::String& id, int maxBeaconAgeMs) const {
    std::lock_guard<std::mutex> lock(peersMtx_);
    auto it = peers_.find(id);
    return it != peers_.end() && it->second.lastBeaconMs > 0
           && nowMs() - it->second.lastBeaconMs <= maxBeaconAgeMs;
}

inline bool InstanceLink::readMaster(const juce::String& id, int maxBeaconAgeMs,
                              std::vector<float>& out, int& rate) const {
    if (!isPeerFresh(id, maxBeaconAgeMs)) return false;
    std::size_t size = 0;
    void* m = openRing(ringNameFor(id), size);
    if (m == nullptr) return false;  // el master se cerró (mapping inexistente)
    bool ok = false;
    auto* hdr = (const instlink::RingHeader*)m;
    if (hdr->magic == instlink::kMagic) {
        const std::int64_t wv = hdr->w.load(std::memory_order_acquire);
        const std::int64_t rv = hdr->r.load(std::memory_order_acquire);
        const std::int64_t cap = (std::int64_t)hdr->cap;
        const std::int64_t lo = std::max(rv, wv - cap);
        const std::int64_t take = std::min(cap, wv - lo);
        if (take > 0) {
            const float* data = (const float*)((const char*)m + instlink::kHeaderSize);
            out.resize((std::size_t)take);
            for (std::int64_t i = 0; i < take; ++i)
                out[(std::size_t)i] = data[static_cast<int>((lo + i) & (cap - 1))];
            rate = (int)hdr->rate;
            ok = (rate > 0);
        }
    }
    closeRing(m, size);
    return ok;
}

inline void InstanceLink::notePeerForTest(const juce::String& id) {
    std::lock_guard<std::mutex> lock(peersMtx_);
    auto& p = peers_[id];
    p.id = id;
    p.lastBeaconMs = nowMs();
}

inline void InstanceLink::clearPeersForTest() {
    std::lock_guard<std::mutex> lock(peersMtx_);
    peers_.clear();
}

inline void InstanceLink::upsertPeer(const juce::String& id) {
    if (id == juce::String(id_.load(std::memory_order_relaxed))) return;  // la propia
    std::lock_guard<std::mutex> lock(peersMtx_);
    auto& p = peers_[id];
    p.id = id;
    p.lastBeaconMs = nowMs();
    pruneLocked();
}

inline void InstanceLink::pruneLocked() {
    const std::int64_t now = nowMs();
    if (now - lastPruneMs_ < 60 * 1000) return;  // cada minuto, como el link móvil
    lastPruneMs_ = now;
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (it->second.lastBeaconMs > 0 && now - it->second.lastBeaconMs > kStaleMs)
            it = peers_.erase(it);
        else
            ++it;
    }
}

inline void InstanceLink::sendBeacon() {
    if (sock_ < 0) return;
    // Mismo patrón que la baliza del móvil (BeaconListener): objeto JSON con
    // el campo "app" como etiqueta de protocolo.
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "acoustical");
    o->setProperty("vst", true);
    o->setProperty("id", juce::String(id_.load(std::memory_order_relaxed)));
    const juce::String payload = juce::JSON::toString(juce::var(o), false);
    const auto* raw = payload.toRawUTF8();
    const int len = payload.getNumBytesAsUTF8();
    sockaddr_in bcast {};
    bcast.sin_family = AF_INET;
    bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    bcast.sin_port = htons((u_int16_t)kBeaconPort);
    sockaddr_in loop {};
    loop.sin_family = AF_INET;
    loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    loop.sin_port = htons((u_int16_t)kBeaconPort);
    sendto(sock_, raw, len, 0, (sockaddr*)&bcast, sizeof bcast);
    sendto(sock_, raw, len, 0, (sockaddr*)&loop, sizeof loop);
}

inline void InstanceLink::run() {
    while (running_.load(std::memory_order_relaxed)) {
        sendBeacon();
        if (sock_ < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        // La recepción tiene un timeout corto (el bucle re-encadena la baliza
        // cada pasada): el thread se para sin bloquear nada al detenerlo.
#if JUCE_WINDOWS
        u_int t = 500;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
#else
        struct timeval tv { 0, 500 * 1000 };
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        char buf[512];
        sockaddr_in from {};
#if JUCE_WINDOWS
        int fromlen = sizeof from;  // (Winsock: recvfrom espera int*)
#else
        socklen_t fromlen = sizeof from;
#endif
        const int n = (int)recvfrom(sock_, buf, sizeof buf - 1, 0, (sockaddr*)&from, &fromlen);
        if (n > 0) {
            buf[n] = 0;
            const auto v = juce::JSON::parse(
                juce::String::fromUTF8(buf, n));
            if (auto* o = v.getDynamicObject()) {
                const juce::String app = o->getProperty("app").toString();
                const juce::String id = o->getProperty("id").toString();
                if (app == "acoustical" && id.isNotEmpty())
                    upsertPeer(id);
            }
        }
    }
}
