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
#include <atomic>

class SyncClient {
public:
    static constexpr int kSyncPort = 41041;

    /** Un intercambio JSON: conecta, escribe, lee hasta que el servidor cierra.
     *  `send` debe ser un objeto JSON (se añade `code` si se pasa).
     *  Devuelve false si la conexión/lectura falla o no hay respuesta válida. */
    static bool exchange(const juce::String& host, int port,
                         const juce::var& send, juce::var& reply,
                         const juce::String& code, int timeoutMs = 1500);

    /** GET HTTP simple; devuelve las cabeceras ("" si no llega una respuesta)
     *  y el cuerpo en body (vacío si el estado no es 200).
     *  `statusCode` (opcional) recibe el código HTTP de la respuesta (p. ej.
     *  401 = código de emparejamiento ausente/no válido), aunque no sea 200. */
    static juce::String httpGet(const juce::String& host, int port, const juce::String& path,
                                juce::MemoryBlock& body, const juce::String& code,
                                int timeoutMs = 3000, int* statusCode = nullptr);

    /** GET HTTP en streaming a un archivo (ideal para el instalador, decenas de MB).
     *  `statusCode` (opcional) recibe el código HTTP aunque no sea 200.
     *  `stopFlag` (opcional): si pasa a true mientras se lee, la descarga se
     *  aborta y se devuelve false (el llamador puede distinguir una
     *  cancelación de un fallo de red releyendo el flag). */
    static bool httpDownloadToFile(const juce::String& host, int port, const juce::String& path,
                                   const juce::File& dest, const juce::String& code,
                                   juce::int64 maxBytes, int* statusCode = nullptr,
                                   const std::atomic<bool>* stopFlag = nullptr);
};
