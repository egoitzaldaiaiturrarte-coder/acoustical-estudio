// BridgeShared.h — protocolo de memoria compartida entre Acoustical Estudio
// (productor/consumidor) y el driver ASIO Acoustical Bridge. Un único bloque
// con anillos de audio float32 intercalados para reproducción (Cubase → app →
// tarjeta real) y captura (tarjeta/cable virtual → app → Cubase).
//
// AVISO DE COMPATIBILIDAD (frentes 4A driver / 4B app):
//  - `sampleRate` y `bufferSize` son ahora `std::atomic<uint32_t>`. El struct
//    YA NO ES COPIABLE POR VALOR (un std::atomic borra el copy-constructor),
//    así que `*header = BridgeHeader{}` NO COMPILA. Para inicializar se usa
//    initializeHeader() (abajo), que escribe campo a campo.
//  - En C++17 std::atomic<T> NO convierte implícitamente a T, así que leer
//    `header->sampleRate` "a pelo" no compila: hay que usar `.load()` / `.store()`
//    (relaxed basta, ver el bloque de memoria). El lado de la app debe adaptar
//    esas lecturas.
#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <string>

namespace bridge {

constexpr unsigned kMagic = 0x41434252;   // 'ACBR'
constexpr unsigned kVersion = 1;
constexpr unsigned kChannels = 2;
constexpr unsigned kRingFrames = 16384;   // ~340 ms @48k

// === NOMBRE DE LA SECCIÓN ===
// Base del nombre. IMPORTANTE que empiece por "Global\" (no por "Local\"): así
// la sección es el MISMO objeto para todas las sesiones de Windows (RDP,
// segundo usuario, servicio). Antes usaba un nombre local (sin prefijo): si el
// driver y la app corrían en sesiones distintas, cada uno creaba su propia
// sección "válida" y operaban sobre memoria que el otro no veía → silencio
// total sin error (split-brain). El prefijo Global\ elimina ese caso.
//
// A la base se le añade un sufijo estable por máquina (los 8 hex del MachineGuid
// de HKLM\SOFTWARE\Microsoft\Cryptography), ver makeSharedName(). Es un
// disyuntivo extra, no la garantía principal: la garantía es el prefijo Global\
// (que ya unifica sesiones) y, como red de seguridad, la validación cruzada de
// instanceId + los heartbeats tick/appTick. Si no se puede leer el MachineGuid,
// se usa la base sin sufijo y la validación de instanceId pilla la divergencia.
constexpr wchar_t kSharedMemoryBase[] = L"Global\\AcousticalBridge";

// Banderas de estado que el driver deja en el header (las lee el cliente).
// Bit 0: el driver detectó que la app dejó de avisar (appTick no avanza).
constexpr uint32_t kStatusAppDead = 1u;

// El struct ya no es copiable (tiene std::atomic). Asumimos un layout portable:
// los campos son de tamaño fijo y alineación natural; en x86/x64 MSVC/clang los
// std::atomic<uint32_t> son lock-free (4 bytes, sin padding). Ambos lados compilan
// contra ESTE header, así que el layout queda idéntico de por sí.
//  * NO reordenar ni insertar campos sin actualizar también el lado de la app.
//  * Los campos de audio se tratan como lock-free; se asume que no hay padding
//    no portable entre procesos (no hay): el layout es el mismo de ambos lados.
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "std::atomic<uint32_t> debe ser lock-free en el target (x86/x64)");

struct BridgeHeader {
    std::atomic<uint32_t> magic = kMagic;      // lock-free; el lector lo carga con acquire
    uint32_t version = kVersion;
    std::atomic<uint32_t> sampleRate = 48000;  // lock-free; usar .load()/.store()
    std::atomic<uint32_t> bufferSize = 512;    // lock-free; usar .load()/.store()
    uint32_t channels = kChannels;
    // Reproducción: la app escribe, el driver lee
    volatile uint64_t playbackWriteIndex = 0;   // en fotogramas
    volatile uint64_t playbackReadIndex = 0;
    // Captura: el driver escribe, la app lee
    volatile uint64_t captureWriteIndex = 0;
    volatile uint64_t captureReadIndex = 0;
    volatile uint32_t appRunning = 0;
    // --- Identidad y vivos (front 4A) ---
    // Lo escribe SOLO el extremo que CREA la sección, una sola vez (antes de
    // publicar magic, ver initializeHeader). El otro extremo solo lo lee. En
    // x86/x64 una carga de 64 bits alineada es atómica, y como se escribe antes
    // de que magic sea válida, el lector solo lo ve cuando el header está listo.
    // Uso: cada extremo, al conectarse, compara el instanceId de la sección con
    // el suyo; tras unos segundos con "magia válida" pero un instanceId ajeno
    // que no reconoce, puede avisar en UI que "el driver parece estar en otra
    // sesión". (La lectura y el aviso los hace el lado de la app, frente 4B.)
    uint64_t instanceId = 0;
    // Heartbeats (long long = 64 bits, alineados → atómicos en x86/x64):
    //  tick    -> el DRIVER lo incrementa cada periodo del worker. La app lo lee
    //              para saber si el driver sigue vivo (getter lo expone 4B).
    //  appTick -> la APP lo incrementa en cada pullCapture/pushPlayback (4B).
    //              El driver lo lee para saber si la app sigue viva y, si no,
    //              congela el playback (silencio) y lo deja visible (kStatusAppDead).
    volatile long long tick = 0;
    volatile long long appTick = 0;
    // Estado visible desde el header: el driver lo escribe, el cliente lo lee.
    // Es un bitfield para admitir más banderas sin crecer el struct.
    volatile uint32_t statusFlags = 0;   // p. ej. kStatusAppDead
};

// Inicializa el header campo a campo (el struct no es copiable). instanceId es
// el del extremo que lo llama = el que CREA la sección. magic se publica al
// final con release: al ser visible como válido, el resto ya está escrito.
// El lector debe cargar magic con acquire y el resto con relaxed (en x86 la
// carga de magic valida el orden; en cualquier caso los campos se escriben antes).
inline void initializeHeader(BridgeHeader* h, uint64_t instanceId) {
    h->version  = kVersion;
    h->sampleRate.store(48000, std::memory_order_relaxed);
    h->bufferSize.store(512,  std::memory_order_relaxed);
    h->channels = kChannels;
    h->playbackWriteIndex = 0;
    h->playbackReadIndex  = 0;
    h->captureWriteIndex  = 0;
    h->captureReadIndex   = 0;
    h->appRunning         = 0;
    h->instanceId         = instanceId;   // solo el creador fija su identidad
    h->tick               = 0;
    h->appTick            = 0;
    h->statusFlags        = 0;
    h->magic.store(kMagic, std::memory_order_release);
}

// Nombre completo de la sección: kSharedMemoryBase [+ \{guid8}] según el
// identificador de máquina. Pura manipulación de string (sin llamadas al SO):
// quien lo llame ya tiene el GUID leído (o "" si no lo pudo leer). El driver y
// la app DEBEN usar el mismo, para que caigan en el mismo objeto.
inline std::wstring makeSharedName(const std::wstring& machineId) {
    std::wstring name = kSharedMemoryBase;
    std::wstring id8;
    for (size_t i = 0; i < machineId.size() && id8.size() < 8; ++i) {
        wchar_t c = machineId[i];
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
        const bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
        if (hex) id8 += c;
    }
    if (!id8.empty()) { name += L"\\"; name += id8; }
    return name;
}

// El bloque total = header + 2 anillos intercalados (playback y capture)
constexpr size_t kPlaybackBytes = kRingFrames * kChannels * sizeof(float);
constexpr size_t kSharedBlockSize = sizeof(BridgeHeader) + kPlaybackBytes * 2;

inline float* playbackRing(BridgeHeader* h) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(h) + sizeof(BridgeHeader));
}
inline float* captureRing(BridgeHeader* h) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(h) + sizeof(BridgeHeader) + kPlaybackBytes);
}

} // namespace bridge
