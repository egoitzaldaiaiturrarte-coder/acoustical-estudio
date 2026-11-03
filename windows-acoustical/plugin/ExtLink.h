// ExtLink.h — El móvil como extensión del VST (v1.11.1).
//
// La app móvil (android-acoustical) corre el MISMO motor del plugin (core C
// vía JNI) y, con esta pieza, ENVIÁ RECURRENTEMENTE su análisis al VST
// instalado en el DAW (Cubase): bandas medidas, SPL, RT60… El VST lo usa
// exactamente igual que ya usa su segunda captura local
// (engine_.setSecondaryCaptureLevels): cada banda toma el nivel más fuerte
// entre la captura del VST y la del móvil, y el corrector converge sobre la
// referencia. El móvil queda así como herramienta de medida externa con
// procesamiento propio: puede ir en la PA, en el coche o a 30 metros con su
// propio micro, y sigue alimentando la corrección que suena en el DAW.
//
// Protocolo (UDP, un solo sentido: móvil -> VST, sin respuesta en esta
// versión):
//   Puerto 41047 en el PC (el 41041/41042/41043/41044 son el link PC de la
//   app y el 41046 la baliza entre instancias — cada canal su puerto).
//   Datagrama = JSON plano:
//     {"app":"acoustical","type":"ext","code":"341207",
//      "bands":[ -6.2, -1.1, ... ], "spl":78.4, "peakSpl":80.0,
//      "rt60":310, "bandsCount":124, "ts":1699999999}
//   "code" = el código de emparejamiento de 6 cifras (Ajustes > PC/Windows
//   en el móvil; el VST solo acepta datagramas cuyo code coincide con su
//   parámetro "Código móvil"). code == 0 en el VST = enlace desactivado
//   (y sin hilo de red arrancado: cero huella en el DAW hasta que se usa).
//
// Hilos (contrato, como el resto del plugin):
//   - El thread del receptor (un std::thread propio, NUNCA el de audio)
//     recibe con timeout de 500 ms y, en cada datagrama válido, actualiza el
//     status (mutex propio, lo lee el editor) y llama al callback onAnalysis
//     — que el procesador conecta al motor: engine_.setSecondaryCaptureLevels
//     (toma stateMutex_, seguro desde cualquier hilo que no sea el de audio,
//     igual que lo hace ya la UI).
//   - El hilo de audio del DAW NO toca nada de esta clase: no hay mutex,
//     socket ni malloc en ese camino.
//
// C++17 puro (sin JUCE): el parser JSON es propio (payload de esquema
// fijo) y los sockets son winsock/POSIX nativos, con lo que el gate dsp de
// la CI (g++ Linux) ejecuta tests de extremo a extremo sobre loopback y la
// sintaxis del camino Windows se comprueba con el stub de winsock2.h de
// tools/fakewin.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define EXT_LINK_WIN_SOCKETS 1
#include <winsock2.h>
#include <ws2tcpip.h>
using extlink_socklen_t = int;
#else
#define EXT_LINK_WIN_SOCKETS 0
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using extlink_socklen_t = socklen_t;
#endif

namespace acoustical {

// Análisis recibido del móvil (un datagrama).
struct ExtAnalysis {
    std::uint32_t code = 0;
    std::vector<float> bands;  // niveles de banda medidos (dB rel. referencia)
    float spl = 0.0f;
    float peakSpl = 0.0f;
    float rt60Ms = 0.0f;
    std::int64_t ts = 0;  // timestamp del móvil (ms)
};

// === Parser mínimo del datagrama (esquema fijo, sin JUCE) ================
// Solo lo que el protocolo necesita: un objeto plano con strings, números y
// UN array de números ("bands"). Sin heap salvo el vector de bandas y los
// strings de clave corta; límites duros (datagrama <= 64 KB, <= 2048 bandas,
// strings <= 64 B) para que un datagrama hostil nunca haga mallocs grandes.
// Devuelve true si el datagrama es un "ext" válido (app+type correctos y
// code presente); out se rellena solo entonces.
inline bool parseExtPacket(const char* buf, int len, ExtAnalysis& out) {
    if (buf == nullptr || len <= 0 || len > 64 * 1024) return false;
    const char* p = buf;
    const char* end = buf + len;
    auto skipWs = [&] { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; };
    skipWs();
    if (p >= end || *p != '{') return false;
    ++p;
    std::map<std::string, std::string> strs;
    std::map<std::string, double> nums;
    while (true) {
        skipWs();
        if (p >= end) return false;
        if (*p == '}') { ++p; break; }
        if (*p == ',') { ++p; skipWs(); }
        if (p >= end || *p != '"') return false;
        ++p;
        std::string key;
        while (p < end && *p != '"') { key.push_back(*p); ++p; }
        if (p >= end) return false;
        ++p;  // la comilla de cierre
        skipWs();
        if (p >= end || *p != ':') return false;
        ++p;
        skipWs();
        if (p >= end) return false;
        if (*p == '"') {  // string
            ++p;
            std::string v;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) ++p;  // escape
                v.push_back(*p);
                ++p;
            }
            if (p >= end) return false;
            ++p;
            strs[key] = v;
        } else if (*p == '[') {  // array de números (solo "bands")
            ++p;
            std::vector<float> arr;
            while (true) {
                skipWs();
                if (p >= end) return false;
                if (*p == ']') { ++p; break; }
                if (*p == ',') { ++p; skipWs(); }
                if (p >= end) return false;
                char* numEnd = nullptr;
                const double d = std::strtod(p, &numEnd);
                if (numEnd == p) return false;
                p = numEnd;
                arr.push_back(static_cast<float>(d));
                if (arr.size() > 2048) return false;
            }
            if (key == "bands") out.bands = std::move(arr);
        } else {  // número
            char* numEnd = nullptr;
            const double d = std::strtod(p, &numEnd);
            if (numEnd == p) return false;
            p = numEnd;
            nums[key] = d;
        }
    }
    skipWs();
    if (p != end) return false;  // basura tras el }
    const auto itApp = strs.find("app");
    const auto itType = strs.find("type");
    const auto itCode = strs.find("code");
    if (itApp == strs.end() || itType == strs.end() || itCode == strs.end()) return false;
    if (itApp->second != "acoustical" || itType->second != "ext") return false;
    const std::string& codeStr = itCode->second;
    if (codeStr.empty() || codeStr.size() > 10) return false;
    for (const char c : codeStr)
        if (c < '0' || c > '9') return false;
    out.code = static_cast<std::uint32_t>(std::strtoul(codeStr.c_str(), nullptr, 10));
    if (out.code == 0) return false;  // code 0 = "sin código" (no válido)
    auto getNum = [&](const char* k, float& dst) {
        const auto it = nums.find(k);
        if (it != nums.end()) dst = static_cast<float>(it->second);
    };
    getNum("spl", out.spl);
    getNum("peakSpl", out.peakSpl);
    getNum("rt60", out.rt60Ms);
    const auto itTs = nums.find("ts");
    if (itTs != nums.end()) out.ts = static_cast<std::int64_t>(itTs->second);
    if (out.bands.empty()) return false;  // sin bandas no hay análisis
    return true;
}

// === El link ================================================================
class ExtLink {
public:
    static constexpr int kExtPort = 41047;

    // (Editor) última recepción vista.
    struct Status {
        std::int64_t lastRxMs = 0;  // 0 = nunca
        std::uint32_t lastCode = 0;
        float spl = 0.0f;
        float peakSpl = 0.0f;
        float rt60Ms = 0.0f;
        int bandsCount = 0;
    };

    ExtLink() = default;
    ~ExtLink() { stop(); }
    ExtLink(const ExtLink&) = delete;
    ExtLink& operator=(const ExtLink&) = delete;

    /** (Hilo de mensajes) Código de emparejamiento. 0 = enlace desactivado
     *  (el hilo de red se para si estaba activo). */
    void setCode(std::uint32_t code) {
        code_.store(code, std::memory_order_release);
        if (code == 0) stop();
    }
    std::uint32_t code() const { return code_.load(std::memory_order_relaxed); }

    /** (Hilo de mensajes) Arranca el hilo receptor (idempotente). */
    void start(int port = kExtPort) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (running_.load(std::memory_order_relaxed)) return;
        port_ = port;
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] { run(); });
    }

    /** (Hilo de mensajes) Para el hilo receptor y libera el socket. */
    void stop() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) thread_.join();
        if (sock_ >= 0) {
#if EXT_LINK_WIN_SOCKETS
            closesocket(sock_);
#else
            close(sock_);
#endif
            sock_ = -1;
        }
    }

    // (El hilo del receptor, en cada datagrama válido) — el procesador lo
    // conecta al motor; NUNCA lo llama el hilo de audio.
    std::function<void(const ExtAnalysis&)> onAnalysis;

    /** (Editor) snapshot del status. */
    Status status() const {
        std::lock_guard<std::mutex> lock(statusMtx_);
        return status_;
    }

    /** (Solo tests de CI) */
    void noteForTest(const ExtAnalysis& a) {
        {
            std::lock_guard<std::mutex> lock(statusMtx_);
            status_.lastRxMs = nowMs();
            status_.lastCode = a.code;
            status_.spl = a.spl;
            status_.peakSpl = a.peakSpl;
            status_.rt60Ms = a.rt60Ms;
            status_.bandsCount = static_cast<int>(a.bands.size());
        }
        if (onAnalysis) onAnalysis(a);
    }

private:
    static std::int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void run() {
#if EXT_LINK_WIN_SOCKETS
        static std::once_flag of;
        std::call_once(of, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
#endif
        sock_ = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_ < 0) { running_.store(false); return; }
#if EXT_LINK_WIN_SOCKETS
        int t = 500;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
#else
        struct timeval tv { 0, 500 * 1000 };
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        sockaddr_in a {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons((std::uint16_t)port_);
        if (bind(sock_, (sockaddr*)&a, sizeof a) != 0) {
            // Puerto ocupado (otro VST en este DAW con el mismo puerto y
            // code != 0, o un test): sin receptor, el enlace no funciona.
#if EXT_LINK_WIN_SOCKETS
            closesocket(sock_);
#else
            close(sock_);
#endif
            sock_ = -1;
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        std::vector<char> buf(16384);
        while (running_.load(std::memory_order_relaxed)) {
            if (sock_ < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); break; }
            sockaddr_in from {};
            extlink_socklen_t fromlen = sizeof from;
            const int n = (int)recvfrom(sock_, buf.data(), (int)buf.size(), 0,
                                        (sockaddr*)&from, &fromlen);
            if (n <= 0) continue;
            buf[(size_t)n] = 0;
            ExtAnalysis a;
            if (!parseExtPacket(buf.data(), n, a)) continue;
            if (a.code != code_.load(std::memory_order_acquire)) continue;  // no es nuestro
            {
                std::lock_guard<std::mutex> lock(statusMtx_);
                status_.lastRxMs = nowMs();
                status_.lastCode = a.code;
                status_.spl = a.spl;
                status_.peakSpl = a.peakSpl;
                status_.rt60Ms = a.rt60Ms;
                status_.bandsCount = static_cast<int>(a.bands.size());
            }
            if (onAnalysis) onAnalysis(a);
        }
    }

    std::atomic<std::uint32_t> code_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    int sock_ = -1;
    int port_ = kExtPort;
    std::mutex mtx_;
    mutable std::mutex statusMtx_;
    Status status_;
};

}  // namespace acoustical
