// SyncClient.cpp — Ver teléfono: la lógica de red es idéntica a la que el
// PC usaba a través del túnel adb; solo cambia la dirección de destino
// (127.0.0.1 → IP del móvil en la red) y el campo/cabecera de código.
#include "SyncClient.h"

namespace {

// Inyecta el código de emparejamiento en el mensaje JSON (si se pasa).
// `send` debe ser un DynamicObject: se modifica en vivo (cada llamada de
// PhoneLink crea su objeto nuevo, así que no se pisa nada).
void injectCode(juce::var& msg, const juce::String& code) {
    if (code.isEmpty()) return;
    if (auto* o = msg.getDynamicObject())
        o->setProperty("code", code);
}

// Cabeceras HTTP base; el código viaja en "X-Acoustical-Code".
juce::String httpRequest(const juce::String& path, const juce::String& code) {
    auto request = "GET " + path + " HTTP/1.0\r\nHost: phone\r\nConnection: close\r\n";
    if (!code.isEmpty()) request += "X-Acoustical-Code: " + code + "\r\n";
    return request + "\r\n";
}

// Drena el socket hasta que el servidor cierre la conexión.
juce::MemoryBlock drain(juce::StreamingSocket& socket, int readTimeoutMs, juce::int64 maxBytes) {
    juce::MemoryBlock raw;
    char buffer[65536];
    for (juce::int64 total = 0; total < maxBytes;) {
        if (socket.waitUntilReady(true, readTimeoutMs) <= 0) break;  // timeout o error
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;  // 0 = cierre, -1 = error
        raw.append(buffer, static_cast<size_t>(n));
        total += n;
    }
    return raw;
}

// Encuentra el fin de cabecera HTTP (\r\n\r\n); -1 si no está.
int findHeaderEnd(const char* p, int size) {
    for (int i = 0; i + 3 < size; ++i)
        if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n')
            return i;
    return -1;
}

// "HTTP/1.0 200 OK..." → 200; -1 si no se entiende.
int parseStatus(const juce::String& headers) {
    return headers.fromFirstOccurrenceOf(" ", false, false)
                    .upToFirstOccurrenceOf(" ", false, false).getIntValue();
}

}  // namespace

bool SyncClient::exchange(const juce::String& host, int port, const juce::var& send,
                          juce::var& reply, const juce::String& code, int timeoutMs) {
    juce::var msg = send;
    injectCode(msg, code);

    juce::StreamingSocket socket;
    if (!socket.connect(host, port, timeoutMs)) return false;
    const auto line = juce::JSON::toString(msg, true);
    const auto utf8 = line.toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return false;
    }
    // TCP es un flujo: la respuesta puede llegar en varios segmentos o superar
    // 64 KB (payloads de 124 bandas). Hay que leer HASTA que el móvil cierre la
    // conexión (el servidor la cierra al terminar).
    const auto raw = drain(socket, 5000, 8 * 1024 * 1024);
    socket.close();
    if (raw.getSize() == 0) return false;
    reply = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(raw.getData()),
                               static_cast<int>(raw.getSize())));
    return !reply.isVoid();
}

juce::String SyncClient::httpGet(const juce::String& host, int port, const juce::String& path,
                                 juce::MemoryBlock& body, const juce::String& code,
                                 int timeoutMs, int* statusCode) {
    body.setSize(0);
    juce::StreamingSocket socket;
    if (!socket.connect(host, port, timeoutMs)) return {};
    const auto utf8 = httpRequest(path, code).toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return {};
    }

    const auto raw = drain(socket, 5000, 16 * 1024 * 1024);
    socket.close();

    const char* p = static_cast<const char*>(raw.getData());
    const int size = static_cast<int>(raw.getSize());
    const int headerEnd = findHeaderEnd(p, size);
    if (headerEnd < 0) return {};

    const auto headers = juce::String::fromUTF8(p, headerEnd);
    const int status = parseStatus(headers);
    if (statusCode != nullptr) *statusCode = status;
    if (status != 200) return {};
    body.append(p + headerEnd + 4, static_cast<size_t>(size - headerEnd - 4));
    return headers;
}

bool SyncClient::httpDownloadToFile(const juce::String& host, int port, const juce::String& path,
                                    const juce::File& dest, const juce::String& code,
                                    juce::int64 maxBytes, int* statusCode,
                                    const std::atomic<bool>* stopFlag) {
    juce::StreamingSocket socket;
    if (!socket.connect(host, port, 3000)) return false;
    const auto utf8 = httpRequest(path, code).toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return false;
    }

    // Timeout más amplio: el instalador son decenas de MB.
    const int kReadTimeoutMs = 15000;
    juce::MemoryBlock pending;
    char buffer[65536];
    int headerEnd = -1;
    while (headerEnd < 0) {
        // Cancelación pedida por el llamador (p. ej. el móvil se fue o se
        // está cerrando la app): no seguir leyendo cabeceras.
        if (stopFlag != nullptr && stopFlag->load(std::memory_order_relaxed)) { socket.close(); return false; }
        if (socket.waitUntilReady(true, kReadTimeoutMs) <= 0) break;
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        pending.append(buffer, static_cast<size_t>(n));
        const char* p = static_cast<const char*>(pending.getData());
        const int size = static_cast<int>(pending.getSize());
        headerEnd = findHeaderEnd(p, size);
        if (headerEnd < 0 && size > 1024 * 1024) { socket.close(); return false; }
    }
    if (headerEnd < 0) { socket.close(); return false; }

    const char* p = static_cast<const char*>(pending.getData());
    const auto headers = juce::String::fromUTF8(p, headerEnd);
    const int status = parseStatus(headers);
    if (statusCode != nullptr) *statusCode = status;
    if (status != 200) { socket.close(); return false; }

    juce::FileOutputStream out(dest);
    if (!out.openedOk()) { socket.close(); return false; }

    const int preBody = static_cast<int>(pending.getSize()) - (headerEnd + 4);
    juce::int64 total = 0;
    bool stopped = stopFlag != nullptr && stopFlag->load(std::memory_order_relaxed);
    if (preBody > 0 && !stopped) {
        out.write(p + headerEnd + 4, static_cast<size_t>(preBody));
        total += preBody;
    }
    while (!stopped && total < maxBytes) {
        // El flag puede levantarse en plena lectura (el móvil se fue, o se
        // cierra la app): se para en la siguiente iteración.
        if (stopFlag != nullptr && stopFlag->load(std::memory_order_relaxed)) { stopped = true; break; }
        if (socket.waitUntilReady(true, kReadTimeoutMs) <= 0) break;
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        out.write(buffer, static_cast<size_t>(n));
        total += n;
    }
    out.flush();
    socket.close();
    // Si se canceló a propósito no se deja un archivo parcial que el llamador
    // pudiera verificar y aceptar: se trata como fallo (el llamador distingue
    // la cancelación releendo su flag).
    if (stopped) return false;
    return total > 0;
}
