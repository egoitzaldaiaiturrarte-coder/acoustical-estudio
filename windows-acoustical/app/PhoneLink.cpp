#include "PhoneLink.h"

#include <juce_cryptography/juce_cryptography.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>

bool installAdbDriverOnce(const juce::File& driverFolder) {
#ifdef _WIN32
    const auto inf = driverFolder.getChildFile("android_winusb.inf");
    if (!inf.existsAsFile()) return false;
    juce::ChildProcess p;
    // --install permite precargar el paquete firmado por Google; la propia
    // instalación de Windows encola el resto al enchufar el móvil.
    return p.start("pnputil /add-driver \"" + inf.getFullPathName() + "\" /install", 0)
            && p.waitForProcessToFinish(20000)
            && p.getExitCode() == 0;
#else
    juce::ignoreUnused(driverFolder);
    return false;
#endif
}

PhoneLink::PhoneLink(juce::File adbExecutable) : adb_(std::move(adbExecutable)) {}

PhoneLink::~PhoneLink() {
    stopWatchdog();
    if (updateThread_.joinable()) updateThread_.join();
}

void PhoneLink::startWatchdog() {
    startTimerHz(1);   // ~cada 1 s; el propio sondeo de adb no necesita más
}

void PhoneLink::stopWatchdog() { stopTimer(); }

juce::String PhoneLink::runAdb(const juce::String& args, int timeoutMs) {
    if (!adb_.existsAsFile()) return {};
    juce::ChildProcess p;
    if (!p.start("\"" + adb_.getFullPathName() + "\" " + args, 0)) return {};
    if (!p.waitForProcessToFinish(static_cast<unsigned>(timeoutMs))) { p.kill(); return {}; }
    return p.readAllProcessOutput().trim();
}

void PhoneLink::timerCallback() { pollDevices(); }

void PhoneLink::setStatus(const juce::String& s) {
    const juce::ScopedLock lock(statusLock_);
    lastInfo_ = s;
}

juce::String PhoneLink::lastSyncInfo() const {
    const juce::ScopedLock lock(statusLock_);
    return lastInfo_;
}

void PhoneLink::pollDevices() {
    if (!adb_.existsAsFile()) {
        if (state_.load() != State::NoAdb) {
            state_.store(State::NoAdb);
            setStatus("adb no encontrado");
        }
        return;
    }

    const auto out = runAdb("devices -l");
    // Salida esperada: "List of devices attached\nSERIAL\tdevice ..."
    juce::String serial;
    for (const auto& line : juce::StringArray::fromLines(out)) {
        const auto t = line.trim();
        if (t.isNotEmpty() && t.containsChar('\t') && t.endsWith("device")) {
            serial = t.upToFirstOccurrenceOf("\t", false, false);
            break;
        }
    }

    if (serial.isEmpty()) {
        deviceSerial_ = {};
        state_.store(State::WaitingForPhone);
        setStatus("Esperando el móvil…");
        driverAttempted_ = false;
        return;
    }

    if (serial != deviceSerial_ || state_.load() != State::Connected) {
        deviceSerial_ = serial;
        // Reenvío de puertos: el PC habla con el servidor de sincronización de
        // la app Android a través de adb (sin Wi-Fi, solo USB).
        runAdb("-s " + serial + " forward tcp:" + juce::String(SYNC_PORT)
               + " tcp:" + juce::String(SYNC_PORT));
        state_.store(State::Connected);
        setStatus("Móvil conectado: " + serial);
        requestSync();
        checkForUpdate();
    }
}

bool PhoneLink::connectAndExchange(const juce::var& send, juce::var& reply) {
    if (deviceSerial_.isEmpty()) return false;
    juce::StreamingSocket socket;
    if (!socket.connect("127.0.0.1", SYNC_PORT, 1500)) return false;
    const auto line = juce::JSON::toString(send, true);
    const auto utf8 = line.toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return false;
    }
    // TCP es un flujo: la respuesta puede llegar en varios segmentos o superar
    // 64 KB (payloads de 124 bandas). Leer una sola vez trunca el JSON. Acumulamos
    // hasta que el móvil cierra la conexión (el servidor la cierra al terminar),
    // con un tope de seguridad para no colgarnos si no cierra.
    juce::MemoryBlock raw;
    char buffer[65536];
    const juce::int64 maxBytes = 8 * 1024 * 1024;
    for (juce::int64 total = 0; total < maxBytes;) {
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        raw.append(buffer, static_cast<size_t>(n));
        total += n;
    }
    socket.close();
    if (raw.getSize() == 0) return false;
    reply = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(raw.getData()),
                               static_cast<int>(raw.getSize())));
    return !reply.isVoid();
}

bool PhoneLink::pushSync(const juce::var& payload) {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "push");
    obj->setProperty("payload", payload);
    if (!connectAndExchange(juce::var(obj), reply)) {
        state_.store(State::SyncError);
        return false;
    }
    setStatus("Ajustes enviados al móvil");
    return true;
}

bool PhoneLink::requestSync() {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "pull");
    if (!connectAndExchange(juce::var(obj), reply)) {
        state_.store(State::SyncError);
        setStatus("Abre Acoustical en el móvil para sincronizar");
        return false;
    }
    setStatus("Sincronizado con el móvil");
    if (onSync) onSync(reply);
    return true;
}

// ============================================================
// Actualización automática desde el móvil
// ============================================================

PhoneLink::WindowsUpdateInfo PhoneLink::lastUpdateInfo() const {
    const std::lock_guard<std::mutex> lock(updateMutex_);
    return updateInfo_;
}

void PhoneLink::checkForUpdate() {
    bool expected = false;
    if (!updateRunning_.compare_exchange_strong(expected, true)) return;
    if (updateThread_.joinable()) updateThread_.join();
    updateThread_ = std::thread([this] {
        runUpdateCheck();
        updateRunning_.store(false);
    });
}

/** GET por el túnel adb; devuelve las cabeceras ("" si falla) y el cuerpo. */
juce::String PhoneLink::httpGet(const juce::String& path, juce::MemoryBlock& body, int timeoutMs) {
    body.setSize(0);
    juce::StreamingSocket socket;
    if (!socket.connect("127.0.0.1", SYNC_PORT, timeoutMs)) return {};
    const auto request = "GET " + path + " HTTP/1.0\r\nHost: phone\r\nConnection: close\r\n\r\n";
    const auto utf8 = request.toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return {};
    }

    juce::MemoryBlock raw;
    char buffer[16384];
    for (;;) {
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        raw.append(buffer, static_cast<size_t>(n));
    }
    socket.close();

    const char* p = static_cast<const char*>(raw.getData());
    const int size = static_cast<int>(raw.getSize());
    int headerEnd = -1;
    for (int i = 0; i + 3 < size; ++i) {
        if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') {
            headerEnd = i;
            break;
        }
    }
    if (headerEnd < 0) return {};

    const auto headers = juce::String::fromUTF8(p, headerEnd);
    const int status = headers.fromFirstOccurrenceOf(" ", false, false)
                            .upToFirstOccurrenceOf(" ", false, false).getIntValue();
    if (status != 200) return {};
    body.append(p + headerEnd + 4, static_cast<size_t>(size - headerEnd - 4));
    return headers;
}

/** Descarga en streaming (ideal para un instalador de decenas de MB). */
bool PhoneLink::httpDownloadToFile(const juce::String& path, const juce::File& dest, juce::int64 maxBytes) {
    juce::StreamingSocket socket;
    if (!socket.connect("127.0.0.1", SYNC_PORT, 3000)) return false;
    const auto request = "GET " + path + " HTTP/1.0\r\nHost: phone\r\nConnection: close\r\n\r\n";
    const auto utf8 = request.toUTF8();
    if (!socket.write(utf8.getAddress(), static_cast<int>(utf8.sizeInBytes() - 1))) {
        socket.close();
        return false;
    }

    juce::MemoryBlock pending;
    char buffer[65536];
    int headerEnd = -1;
    while (headerEnd < 0) {
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        pending.append(buffer, static_cast<size_t>(n));
        const char* p = static_cast<const char*>(pending.getData());
        const int size = static_cast<int>(pending.getSize());
        for (int i = 0; i + 3 < size; ++i) {
            if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') {
                headerEnd = i;
                break;
            }
        }
        if (headerEnd < 0 && size > 1024 * 1024) { socket.close(); return false; }
    }
    if (headerEnd < 0) { socket.close(); return false; }

    const char* p = static_cast<const char*>(pending.getData());
    const auto headers = juce::String::fromUTF8(p, headerEnd);
    const int status = headers.fromFirstOccurrenceOf(" ", false, false)
                            .upToFirstOccurrenceOf(" ", false, false).getIntValue();
    if (status != 200) { socket.close(); return false; }

    juce::FileOutputStream out(dest);
    if (!out.openedOk()) { socket.close(); return false; }

    const int preBody = static_cast<int>(pending.getSize()) - (headerEnd + 4);
    juce::int64 total = 0;
    if (preBody > 0) {
        out.write(p + headerEnd + 4, static_cast<size_t>(preBody));
        total += preBody;
    }
    while (total < maxBytes) {
        const int n = socket.read(buffer, sizeof(buffer), false);
        if (n <= 0) break;
        out.write(buffer, static_cast<size_t>(n));
        total += n;
    }
    out.flush();
    socket.close();
    return total > 0;
}

juce::String PhoneLink::installedVersion() const {
#ifdef JUCE_WINDOWS
    auto v = juce::WindowsRegistry::getValue("HKLM\\SOFTWARE\\Acoustical\\Version");
    if (v.isEmpty())
        v = juce::WindowsRegistry::getValue("HKLM\\SOFTWARE\\WOW6432Node\\Acoustical\\Version");
    return v.isEmpty() ? juce::String("0.0.0") : v.trim();
#else
    return "0.0.0";
#endif
}

int PhoneLink::compareVersions(const juce::String& a, const juce::String& b) {
    const auto ta = juce::StringArray::fromTokens(a, ".", {});
    const auto tb = juce::StringArray::fromTokens(b, ".", {});
    const int n = std::max(ta.size(), tb.size());
    for (int i = 0; i < n; ++i) {
        const int va = i < ta.size() ? ta[i].getIntValue() : 0;
        const int vb = i < tb.size() ? tb[i].getIntValue() : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

/** Lanza el instalador con permisos de administrador: Windows muestra el UAC,
 *  así que el usuario siempre da el visto bueno final. */
bool PhoneLink::launchInstaller(const juce::File& installer) const {
#ifdef JUCE_WINDOWS
    const auto path = installer.getFullPathName();
    const auto params = juce::String(
        "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS");
    SHELLEXECUTEINFOW sei {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_DEFAULT;
    sei.lpVerb = L"runas";
    sei.lpFile = path.toWideCharPointer();
    sei.lpParameters = params.toWideCharPointer();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
#else
    juce::ignoreUnused(installer);
    return false;
#endif
}

void PhoneLink::runUpdateCheck() {
    setStatus("Comprobando versión con el móvil…");
    juce::MemoryBlock body;
    if (httpGet("/manifest", body).isEmpty()) return; // el móvil aún no sirve HTTP: silencio

    const auto manifest = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(body.getData()), static_cast<int>(body.getSize())));
    auto* obj = manifest.getDynamicObject();
    if (obj == nullptr) { setStatus("Respuesta del móvil no válida"); return; }

    WindowsUpdateInfo info;
    if (auto* wObj = obj->getProperty("windows").getDynamicObject()) {
        info.version = wObj->getProperty("version").toString();
        info.sha256 = wObj->getProperty("sha256").toString();
        const auto sizeVar = wObj->getProperty("size");
        info.sizeBytes = sizeVar.isVoid() ? 0 : static_cast<juce::int64>(static_cast<double>(sizeVar));
        const auto payloadVar = wObj->getProperty("hasPayload");
        info.hasPayload = payloadVar.isVoid() ? false : static_cast<bool>(payloadVar);
        info.url = wObj->getProperty("url").toString();
    }
    {
        const std::lock_guard<std::mutex> lock(updateMutex_);
        updateInfo_ = info;
    }

    const auto local = installedVersion();
    if (info.version.isEmpty() || compareVersions(info.version, local) <= 0) {
        setStatus("Acoustical al día (v" + local + ")");
        return;
    }

    if (!info.hasPayload || info.sha256.isEmpty()) {
        setStatus("Versión " + info.version + " en el móvil: descárgala en la app (Ajustes > PC/Windows)");
        return;
    }

    // Todo a una carpeta temporal dedicada; nunca se ejecuta nada más que el
    // instalador verificado con su SHA-256.
    setStatus("Actualizando a " + info.version + ": descargando por USB…");
    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("AcousticalUpdate");
    tempDir.deleteRecursively();
    if (!tempDir.createDirectory().wasOk()) {
        setStatus("No se pudo preparar la actualización");
        return;
    }
    const auto installer = tempDir.getChildFile("AcousticalEstudioSetup.exe");
    if (!httpDownloadToFile("/payload", installer, MAX_PAYLOAD_BYTES)) {
        setStatus("Descarga fallida: vuelve a conectar el móvil");
        tempDir.deleteRecursively();
        return;
    }

    // Verificación estricta antes de ejecutar: tamaño y SHA-256 del manifest
    if (info.sizeBytes > 0 && installer.getSize() != info.sizeBytes) {
        setStatus("Actualización cancelada: tamaño incorrecto");
        tempDir.deleteRecursively();
        return;
    }
    const auto hash = juce::SHA256(installer).toHexString();
    if (hash.equalsIgnoreCase(info.sha256) == false) {
        setStatus("Actualización cancelada: la verificación SHA-256 no coincide");
        tempDir.deleteRecursively();
        return;
    }

    setStatus("Instalando " + info.version + "… (Windows pedirá permiso)");
    if (launchInstaller(installer))
        setStatus("Versión " + info.version + " instalándose: Acoustical se reiniciará");
    else
        setStatus("No se pudo iniciar el instalador (falta el permiso de administrador)");
}
