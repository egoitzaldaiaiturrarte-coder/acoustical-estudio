// StartupLog.h — registro de arranque/diagnóstico de la app.
//
// Escribe hitos con marca de tiempo en:
//     %AppData%\Acoustical\startup.log      (Windows)
//     ~/.config/Acoustical/startup.log      (Linux, al compilar en desarrollo)
// Es el mismo sitio que el resto de los datos de la app (audio.xml, etc.).
//
// El archivo se reinicia solo si supera 256 KB, para no crecer sin más.
// `startuplog::log()` es seguro desde cualquier hilo (un mutex interno,
// una escritura pequeña por llamada).
//
// Nota: el callback de audio en sí NO escribe nada aquí: solo actualiza un
// contador atómico que el timer de UI volca al log.
#pragma once

#include <juce_core/juce_core.h>

#include <mutex>

namespace startuplog {

/** Archivo de log (creado a la primera escritura). Vacío si no hay sitio. */
inline juce::File logFile() {
    static juce::File file = [] {
        const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        if (base == juce::File()) return juce::File();
        auto dir = base.getChildFile("Acoustical");
        dir.createDirectory();
        return dir.getChildFile("startup.log");
    }();
    return file;
}

/** Añade una línea "dd/mm/aaaa hh:mm:ss  mensaje". Silencioso si no hay archivo. */
inline void log(const juce::String& msg) {
    static std::mutex mtx;   // un solo mutex para todos los hilos
    const std::lock_guard<std::mutex> lock(mtx);
    const juce::File f = logFile();
    if (f == juce::File()) return;
    if (f.existsAsFile() && f.getSize() > 262144)
        f.deleteFile();
    f.appendText(juce::Time::getCurrentTime().toString(true, true, true, true)
                 + "  " + msg + juce::newLine, true);
}

}  // namespace startuplog
