// PhoneLink.cpp
#include "PhoneLink.h"
#include "SyncClient.h"

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

PhoneLink::PhoneLink(juce::File adbExecutable) : adb_(std::move(adbExecutable)) {
    beacon_ = std::make_unique<BeaconListener>(BEACON_PORT);
    loadConfig();
}

PhoneLink::~PhoneLink() {
    stopWatchdog();
    if (updateThread_.joinable()) updateThread_.join();
}

void PhoneLink::startWatchdog() {
    startTimerHz(1);   // ~cada 1 s; ni la baliza ni el sondeo de adb piden más
    beacon_->start([](const juce::String&, const juce::var&) {});
}

void PhoneLink::stopWatchdog() {
    stopTimer();
    if (beacon_) beacon_->stop();
}

juce::String PhoneLink::runAdb(const juce::String& args, int timeoutMs) {
    if (!adb_.existsAsFile()) return {};
    juce::ChildProcess p;
    if (!p.start("\"" + adb_.getFullPathName() + "\" " + args, 0)) return {};
    if (!p.waitForProcessToFinish(static_cast<unsigned>(timeoutMs))) { p.kill(); return {}; }
    return p.readAllProcessOutput().trim();
}

namespace {
// Validación estricta de la IP manual (a.b.c.d, octetos 0-255)
bool isPlainIp(const juce::String& s) {
    const auto parts = juce::StringArray::fromTokens(s, ".");
    if (parts.size() != 4) return false;
    for (const auto& p : parts) {
        if (p.isEmpty() || p.length() > 3) return false;
        for (int i = 0; i < p.length(); ++i)
            if (!juce::CharacterFunctions::isDigit(p.getCharPointer()[i])) return false;
        const int v = p.getIntValue();
        if (v < 0 || v > 255) return false;
    }
    return true;
}
}  // namespace

// ============================================================
// Selección de transporte: Wi-Fi → IP manual → USB
// ============================================================

void PhoneLink::timerCallback() { pollDevices(); }

void PhoneLink::pollDevices() {
    decideEndpoint();

    // Un fallo puntual de red no debe dejar el enlace atascado: se reintenta
    if (state_.load() == State::SyncError) {
        const juce::ScopedLock lock(endpointLock_);
        if (!endpointHost_.isEmpty()) state_.store(State::Connected);
    }

    // Sondeo periódico de sincronización: trae los comandos del Hub que el
    // móvil haya encolado (viajan en la respuesta "sync")
    if (state_.load() == State::Connected && ++syncPollCounter_ >= 3) {
        syncPollCounter_ = 0;
        requestSync();
    }
}

void PhoneLink::decideEndpoint() {
    // 1) Wi-Fi: el móvil anuncia su IP con la baliza UDP
    if (beacon_ && beacon_->isFresh(BEACON_TTL_MS)) {
        const auto ip = beacon_->lastIp();
        if (ip.isNotEmpty()) {
            if (endpointChanged(ip, Transport::WiFi)) {
                setStatus("Móvil conectado (Wi-Fi): " + ip);
                requestSync();
                checkForUpdate();
            }
            return;
        }
    }

    // 2) IP del móvil puesta a mano (routers que bloquean la baliza)
    const juce::String manualIp = manualPhoneIp();  // copia thread-safe
    if (isPlainIp(manualIp)) {
        if (endpointChanged(manualIp, Transport::ManualIp)) {
            setStatus("Móvil por IP manual: " + manualIp);
            requestSync();
            checkForUpdate();
        }
        return;
    }

    // 3) Respaldo USB: detección y túnel por adb, como antes
    if (!adb_.existsAsFile()) {
        if (endpointChanged({}, Transport::None)) {
            state_.store(State::NoAdb);
            setStatus("Esperando el móvil (Wi-Fi)… (adb no disponible)");
        }
        return;
    }

    const auto out = runAdb("devices -l");
    // Salida esperada: "List of devices attached\nSERIAL\tdevice usb:1-1 ..."
    // Con "-l" el estado NO está al final de la línea (termina en
    // transport_id:N), así que endsWith("device") nunca coincidía y el
    // móvil no se detectaba jamás. Se analiza el campo de estado.
    juce::String serial;
    for (const auto& line : juce::StringArray::fromLines(out)) {
        const auto t = line.trim();
        if (t.isEmpty() || !t.containsChar('\t')) continue;
        const auto s = t.upToFirstOccurrenceOf("\t", false, false).trim();
        const auto st = t.fromFirstOccurrenceOf("\t", false, false).trim();
        // Estados de adb: device, offline, unauthorized, nopermissions…
        if (s.isNotEmpty() && st.startsWith("device")) {
            serial = s;
            break;
        }
    }

    if (serial.isEmpty()) {
        deviceSerial_ = {};
        if (endpointChanged({}, Transport::None))
            setStatus("Esperando el móvil (Wi-Fi o USB)…");
        return;
    }

    deviceSerial_ = serial;
    if (endpointChanged("127.0.0.1", Transport::USB)) {
        // Reenvío de puertos: el PC habla con el servidor de sincronización de
        // la app Android a través de adb (sin Wi-Fi, solo USB).
        runAdb("-s " + serial + " forward tcp:" + juce::String(SYNC_PORT)
               + " tcp:" + juce::String(SYNC_PORT));
        setStatus("Móvil conectado (USB): " + serial);
        requestSync();
        checkForUpdate();
    }
}

bool PhoneLink::endpointChanged(const juce::String& host, PhoneLink::Transport t) {
    const juce::ScopedLock lock(endpointLock_);
    if (!host.isEmpty() && host == endpointHost_ && t == transport_) return false;
    if (host.isEmpty() && endpointHost_.isEmpty() && transport_ == Transport::None) return false;
    endpointHost_ = host;
    transport_ = t;
    state_.store(host.isEmpty() ? State::WaitingForPhone : State::Connected);
    return true;
}

PhoneLink::Transport PhoneLink::transport() const {
    const juce::ScopedLock lock(endpointLock_);
    return transport_;
}

juce::String PhoneLink::deviceSerial() const {
    const juce::ScopedLock lock(endpointLock_);
    if (transport_ == Transport::USB) return deviceSerial_;
    return endpointHost_;
}

void PhoneLink::setStatus(const juce::String& s) {
    const juce::ScopedLock lock(statusLock_);
    lastInfo_ = s;
}

juce::String PhoneLink::lastSyncInfo() const {
    const juce::ScopedLock lock(statusLock_);
    return lastInfo_;
}

// ============================================================
// Sincronización (por el transporte activo)
// ============================================================

bool PhoneLink::exchange(const juce::var& send, juce::var& reply) {
    juce::String host;
    {
        const juce::ScopedLock lock(endpointLock_);
        if (endpointHost_.isEmpty()) return false;
        host = endpointHost_;
    }
    if (!SyncClient::exchange(host, SYNC_PORT, send, reply, pairCode())) {
        if (state_.load() == State::Connected) state_.store(State::SyncError);
        return false;
    }
    return true;
}

bool PhoneLink::pushSync(const juce::var& payload) {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "push");
    obj->setProperty("payload", payload);
    if (!exchange(juce::var(obj), reply)) {
        setStatus("Abre Acoustical en el móvil para sincronizar");
        return false;
    }
    setStatus("Ajustes enviados al móvil");
    return true;
}

bool PhoneLink::requestSync() {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "pull");
    if (!exchange(juce::var(obj), reply)) {
        setStatus("Abre Acoustical en el móvil para sincronizar");
        return false;
    }
    // El móvil sin emparejar responde con un error de código: se muestra una
    // vez y el sondeo de 3 s reintenta hasta que el usuario lo introduzca.
    if (auto* o = reply.getDynamicObject();
        o != nullptr && o->getProperty("type").toString() == "pair") {
        setStatus("Introduce el código del móvil en Ajustes > Móvil");
        return false;
    }
    setStatus("Sincronizado con el móvil");
    if (onSync) onSync(reply);
    return true;
}

// ============================================================
// Configuración persistente (código de emparejamiento + IP manual)
// ============================================================

juce::File PhoneLink::configPath() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Acoustical").getChildFile("phonelink.json");
}

void PhoneLink::loadConfig() {
    const auto f = configPath();
    if (!f.existsAsFile()) return;
    if (auto* o = juce::JSON::parse(f.loadFileAsString()).getDynamicObject()) {
        pairCode_ = o->getProperty("pairCode").toString();
        manualIp_ = o->getProperty("manualIp").toString();
    }
}

void PhoneLink::saveConfig() {
    juce::String code, ip;
    {
        const juce::ScopedLock lock(configLock_);
        code = pairCode_;
        ip = manualIp_;
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("pairCode", code);
    o->setProperty("manualIp", ip);
    configPath().getParentDirectory().createDirectory();
    configPath().replaceWithText(juce::JSON::toString(juce::var(o), true));
}

void PhoneLink::setPairCode(const juce::String& code) {
    {
        const juce::ScopedLock lock(configLock_);
        pairCode_ = code.trim();
    }
    saveConfig();
}

void PhoneLink::setManualPhoneIp(const juce::String& ip) {
    {
        const juce::ScopedLock lock(configLock_);
        manualIp_ = ip.trim();
    }
    saveConfig();
}

juce::String PhoneLink::pairCode() const {
    const juce::ScopedLock lock(configLock_);
    return pairCode_;
}

juce::String PhoneLink::manualPhoneIp() const {
    const juce::ScopedLock lock(configLock_);
    return manualIp_;
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

juce::String PhoneLink::httpGet(const juce::String& path, juce::MemoryBlock& body, int timeoutMs) {
    juce::String host;
    {
        const juce::ScopedLock lock(endpointLock_);
        host = endpointHost_;
    }
    if (host.isEmpty()) return {};
    return SyncClient::httpGet(host, SYNC_PORT, path, body, pairCode(), timeoutMs);
}

bool PhoneLink::httpDownloadToFile(const juce::String& path, const juce::File& dest, juce::int64 maxBytes) {
    juce::String host;
    {
        const juce::ScopedLock lock(endpointLock_);
        host = endpointHost_;
    }
    if (host.isEmpty()) return false;
    return SyncClient::httpDownloadToFile(host, SYNC_PORT, path, dest, pairCode(), maxBytes);
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
    setStatus("Actualizando a " + info.version + ": descargando del móvil…");
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
