// SyncClient.h — Cliente TCP/HTTP del servidor de sincronización del móvil
// (puerto 41041). Puro transporte: no sabe nada de adb, Wi-Fi ni USB, así
// que la misma pieza corre en el PC y en el test de protocolo de Linux.
//
// El móvil acepta dos protocolos en el mismo puerto:
//  - JSON simple (una conexión = un mensaje): {"type":"push"/"pull",...}
//  - HTTP/1.0: GET /manifest, GET /payload, GET/POST /sync
// Si `code` no está vacío se inyecta en cada mensaje (emparejamiento Wi-Fi).
#pragma once

#include <juce_core/juce_core.h>

class SyncClient {
public:
    static constexpr int kSyncPort = 41041;

    /** Un intercambio JSON: conecta, escribe, lee hasta que el servidor cierra.
     *  `send` debe ser un objeto JSON (se añade `code` si se pasa).
     *  Devuelve false si la conexión/lectura falla o no hay respuesta válida. */
    static bool exchange(const juce::String& host, int port,
                         const juce::var& send, juce::var& reply,
                         const juce::String& code, int timeoutMs = 1500);

    /** GET HTTP simple; devuelve las cabeceras ("" si falla) y el cuerpo en body. */
    static juce::String httpGet(const juce::String& host, int port, const juce::String& path,
                                juce::MemoryBlock& body, const juce::String& code,
                                int timeoutMs = 3000);

    /** GET HTTP en streaming a un archivo (ideal para el instalador, decenas de MB). */
    static bool httpDownloadToFile(const juce::String& host, int port, const juce::String& path,
                                   const juce::File& dest, const juce::String& code,
                                   juce::int64 maxBytes);
};
