// PhoneLink.cpp
#include "PhoneLink.h"
#include "StartupLog.h"
#include "SyncClient.h"

#include <juce_cryptography/juce_cryptography.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <future>
#include <memory>

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
    startuplog::log(juce::String::fromUTF8("phonelink: oyente de baliza creado"));
    loadConfig();
    startuplog::log(juce::String::fromUTF8("phonelink: config cargada"));
    // Puerta de emparejamiento de la baliza: solo se fía de una baliza cuyo
    // campo "code" coincide con el código que el usuario pegó en Ajustes > Móvil.
    // Antes, el oyente aceptaba cualquier baliza con app=="acoustical" y el PC
    // usaba la IP del emisor para la comprobación de actualización: cualquier
    // dispositivo de la LAN podía apuntar la descarga del instalador a su propio
    // servidor (RCE). El validador lee pairCode_ en vivo, así que se actualiza
    // solo cuando el usuario cambia el código (setPairCode).
    beacon_->setCodeValidator([this](const juce::String& beaconCode) {
        juce::String mine;
        {
            const juce::ScopedLock lock(configLock_);
            mine = pairCode_;
        }
        // Sin código emparejado no se fía de ninguna baliza (default seguro).
        return !mine.isEmpty() && beaconCode == mine;
    });
}

PhoneLink::~PhoneLink() {
    stopWatchdog();
    // Si el hilo de actualización va en plena descarga del instalador, pedirle
    // que pare: sin esto, el join de abajo se quedaría esperando a que el
    // descargador termine (cientos de MB) justo al cerrar la app.
    if (updateRunning_.load()) downloadStopRequested_.store(true);
    if (updateThread_.joinable()) updateThread_.join();
}

void PhoneLink::startWatchdog() {
    startTimerHz(1);   // ~cada 1 s; ni la baliza ni el sondeo de adb piden más
    beacon_->start([this](const juce::String& ip, const juce::var& info) {
        if (onBeacon) onBeacon(ip, info);
    });
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

// Un serial de `adb devices` es dato externo y termina interpolado en la
// línea de comandos de la siguiente llamada adb (runAdb). Si no se restringe
// el alfabeto, un adb alterado o con un bug que imprimiera un "serial" con
// espacios, comillas o punto y coma podría inyectar argumentos en esa llamada.
// Los serials USB reales solo usan alfanuméricos (con '-' o '_' de
// separador). Los serials "ip:port" de un adb de red NO se aceptan: esta
// vía es el respaldo USB; el Wi-Fi ya tiene sus propios caminos (baliza/IP).
bool isAdbSerial(const juce::String& s) {
    if (s.isEmpty()) return false;
    auto isAlnum = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (!isAlnum(s[0])) return false;
    for (int i = 1; i < s.length(); ++i) {
        const char c = s[i];
        if (!isAlnum(c) && c != '-' && c != '_') return false;
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
            // Vía rápida: reinicia el refresco del sondeo adb (ver los
            // miembros adbProbeEvery_/adbProbeSkip_).
            adbProbeEvery_ = 1;
            adbProbeSkip_ = 0;
            if (endpointChanged(ip, Transport::WiFi)) {
                setStatus(juce::String::fromUTF8("Móvil conectado (Wi-Fi): ") + ip);
                requestSync();
                checkForUpdate();
            }
            return;
        }
    }

    // 2) IP del móvil puesta a mano (routers que bloquean la baliza)
    const juce::String manualIp = manualPhoneIp();  // copia thread-safe
    if (isPlainIp(manualIp)) {
        // Vía rápida: reinicia el refresco del sondeo adb.
        adbProbeEvery_ = 1;
        adbProbeSkip_ = 0;
        if (endpointChanged(manualIp, Transport::ManualIp)) {
            setStatus(juce::String::fromUTF8("Móvil por IP manual: ") + manualIp);
            requestSync();
            checkForUpdate();
        }
        return;
    }

    // 3) Respaldo USB: detección y túnel por adb, como antes
    if (!adb_.existsAsFile()) {
        if (endpointChanged({}, Transport::None)) {
            state_.store(State::NoAdb);
            setStatus(juce::String::fromUTF8("Esperando el móvil (Wi-Fi)… (adb no disponible)"));
            requestDownloadStop();
        }
        return;
    }

    // El sondeo "adb devices -l" arranca un proceso hijo: para no repetirlo
    // a cada tick para siempre (sin móvil se martiljeaba disco y CPU), el
    // intervalo se refrena en refuerzo exponencial mientras no hay baliza ni
    // IP manual: cada 2, 4, 8, 16, 32 ticks, con tope de 60 (~60 s). Las vías
    // rápidas de arriba lo reinician a 1, de modo que al caer al USB el
    // primer sondeo es inmediato.
    if (adbProbeSkip_ > 0) {
        --adbProbeSkip_;
        return;
    }
    adbProbeSkip_ = adbProbeEvery_;
    adbProbeEvery_ = std::min(60, adbProbeEvery_ * 2);

    const auto out = runAdb("devices -l");
    // Salida esperada: "List of devices attached\nSERIAL\tdevice usb:1-1 ..."
    // Con "-l" el estado NO está al final de la línea (termina en
    // transport_id:N), así que endsWith("device") nunca coincidía y el
    // móvil no se detectaba jamás. Se analiza el campo de estado.
    juce::String serial;
    juce::String rejected;
    for (const auto& line : juce::StringArray::fromLines(out)) {
        const auto t = line.trim();
        if (t.isEmpty() || !t.containsChar('\t')) continue;
        const auto s = t.upToFirstOccurrenceOf("\t", false, false).trim();
        const auto st = t.fromFirstOccurrenceOf("\t", false, false).trim();
        // Estados de adb: device, offline, unauthorized, nopermissions…
        if (s.isEmpty() || !st.startsWith("device")) continue;
        // Un "device" cuyo serial no entra en el alfabeto real no se toca
        // (isAdbSerial): se anota y se prueba la siguiente línea, en vez de
        // inyectarlo en la llamada de forward de abajo.
        if (!isAdbSerial(s)) {
            if (rejected.isEmpty()) rejected = s;
            continue;
        }
        serial = s;
        break;
    }
    if (serial.isEmpty() && !rejected.isEmpty())
        startuplog::log(juce::String::fromUTF8("phonelink: serial adb rechazado (alfabeto): ") + rejected);

    if (serial.isEmpty()) {
        deviceSerial_ = {};
        if (endpointChanged({}, Transport::None)) {
            // El móvil se fue: si la descarga del instalador sigue en marcha,
            // pedirle que pare (no tiene destino al que seguir leyendo).
            requestDownloadStop();
            setStatus(juce::String::fromUTF8("Esperando el móvil (Wi-Fi o USB)…"));
        }
        return;
    }

    deviceSerial_ = serial;
    if (endpointChanged("127.0.0.1", Transport::USB)) {
        // Reenvío de puertos: el PC habla con el servidor de sincronización de
        // la app Android a través de adb (sin Wi-Fi, solo USB).
        runAdb("-s " + serial + " forward tcp:" + juce::String(SYNC_PORT)
               + " tcp:" + juce::String(SYNC_PORT));
        setStatus(juce::String::fromUTF8("Móvil conectado (USB): ") + serial);
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

bool PhoneLink::pushSync(const juce::var& payload, const juce::String& statusMsg, juce::var* replyOut) {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "push");
    obj->setProperty("payload", payload);
    if (!exchange(juce::var(obj), reply)) {
        setStatus(juce::String::fromUTF8("Abre Acoustical en el móvil para sincronizar"));
        return false;
    }
    setStatus(statusMsg);
    if (replyOut != nullptr) *replyOut = reply;
    else if (onSync) onSync(reply);
    return true;
}

bool PhoneLink::requestSync(juce::var* replyOut) {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "pull");
    if (!exchange(juce::var(obj), reply)) {
        setStatus(juce::String::fromUTF8("Abre Acoustical en el móvil para sincronizar"));
        return false;
    }
    // El móvil sin emparejar responde con un error de código: se muestra una
    // vez y el sondeo de 3 s reintenta hasta que el usuario lo introduzca.
    if (auto* o = reply.getDynamicObject();
        o != nullptr && o->getProperty("type").toString() == "pair") {
        // hasCode=false: el móvil aún no ha generado código (app abierta pero
        // sin pasar nunca por Ajustes > PC/Windows) → decirle que lo abra.
        // hasCode=true o campo ausente (app antigua) → solo no coincide.
        const auto hasCodeProp = o->getProperty("hasCode");
        const bool hasCode = hasCodeProp.isVoid() || hasCodeProp.toString() != "false";
        setStatus(hasCode
            ? juce::String::fromUTF8("El código no coincide: mira el del móvil (Ajustes > PC/Windows) y pégalo en Ajustes > Móvil")
            : juce::String::fromUTF8("Abre Ajustes > PC/Windows en el móvil (ahí generará su código) y pégalo en Ajustes > Móvil"));
        return false;
    }
    setStatus(juce::String::fromUTF8("Sincronizado con el móvil"));
    if (replyOut != nullptr) *replyOut = reply;
    else if (onSync) onSync(reply);
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
    // `parsed` debe vivir todo el bloque: getDynamicObject() apunta a su
    // interior. Si se dejara como temporal, moriría al terminar la
    // condición del if y el cuerpo usaría un puntero colgante.
    const auto parsed = juce::JSON::parse(f.loadFileAsString());
    if (auto* o = parsed.getDynamicObject()) {
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
    // Código nuevo: la puerta de la baliza y la auto-actualización empiezan de
    // cero (un par de 401 acumulados con el código viejo no debe frenar el
    // nuevo, y viceversa).
    update401Count_ = 0;
    update401Stopped_ = false;
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
    // Frenada por 401 (código del móvil no válido N veces seguidas): dejar de
    // intentar hasta que el usuario cambie el código (setPairCode la levanta).
    if (update401Stopped_) return;
    bool expected = false;
    if (!updateRunning_.compare_exchange_strong(expected, true)) return;
    if (updateThread_.joinable()) updateThread_.join();
    updateThread_ = std::thread([this] {
        runUpdateCheck();
        updateRunning_.store(false);
    });
}

juce::String PhoneLink::httpGet(const juce::String& path, juce::MemoryBlock& body, int timeoutMs,
                                int* statusOut) {
    juce::String host;
    {
        const juce::ScopedLock lock(endpointLock_);
        host = endpointHost_;
    }
    if (host.isEmpty()) return {};
    return SyncClient::httpGet(host, SYNC_PORT, path, body, pairCode(), timeoutMs, statusOut);
}

bool PhoneLink::httpDownloadToFile(const juce::String& path, const juce::File& dest, juce::int64 maxBytes,
                                   std::atomic<bool>* stopFlag) {
    juce::String host;
    {
        const juce::ScopedLock lock(endpointLock_);
        host = endpointHost_;
    }
    if (host.isEmpty()) return false;
    return SyncClient::httpDownloadToFile(host, SYNC_PORT, path, dest, pairCode(),
                                          maxBytes, nullptr, stopFlag);
}

void PhoneLink::requestDownloadStop() {
    // Solo si hay un intento de actualización en curso: el flag cancela la
    // descarga del instalador si sigue en marcha (downloadStopRequested_).
    if (updateRunning_.load()) downloadStopRequested_.store(true);
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
    setStatus(juce::String::fromUTF8("Comprobando versión con el móvil…"));
    int manifestStatus = 0;
    juce::MemoryBlock body;
    if (httpGet("/manifest", body, 3000, &manifestStatus).isEmpty()) {
        // 401 = el móvil rechazó la petición (código de emparejamiento ausente o
        // no válido). Contamos los 401 consecutivos y, tras kMaxUpdate401,
        // frenamos la auto-actualización: repetir el mismo código malo no lleva
        // a nada y parecería un martilleo al móvil. Un 200 lo reinicia (abajo)
        // y cambiar el código (setPairCode) también.
        // El resto de fallos (aún no sirve HTTP, timeout) siguen en silencio.
        if (manifestStatus == 401) {
            const int count = update401Count_.fetch_add(1) + 1;
            if (count >= kMaxUpdate401) {
                update401Stopped_ = true;
                setStatus(juce::String::fromUTF8("Auto-actualización detenida: el código del móvil no coincide (Ajustes > Móvil)"));
            } else {
                setStatus(juce::String::fromUTF8("El código del móvil no coincide (Ajustes > Móvil): actualización en espera"));
            }
        }
        return;
    }
    update401Count_ = 0;   // 200: el código es válido; reinicia el contador

    const auto manifest = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(body.getData()), static_cast<int>(body.getSize())));
    auto* obj = manifest.getDynamicObject();
    if (obj == nullptr) { setStatus(juce::String::fromUTF8("Respuesta del móvil no válida")); return; }

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
        setStatus(juce::String::fromUTF8("Acoustical al día (v") + local + ")");
        return;
    }

    if (!info.hasPayload || info.sha256.isEmpty()) {
        setStatus(juce::String::fromUTF8("Versión ") + info.version + juce::String::fromUTF8(" en el móvil: descárgala en la app (Ajustes > PC/Windows)"));
        return;
    }

    // Todo a una carpeta temporal dedicada; nunca se ejecuta nada más que el
    // instalador verificado con su SHA-256.
    setStatus("Actualizando a " + info.version + juce::String::fromUTF8(": descargando del móvil…"));
    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("AcousticalUpdate");
    tempDir.deleteRecursively();
    if (!tempDir.createDirectory().wasOk()) {
        setStatus(juce::String::fromUTF8("No se pudo preparar la actualización"));
        return;
    }
    const auto installer = tempDir.getChildFile("AcousticalEstudioSetup.exe");
    // Descarga con posibilidad de cancelación: si el móvil se va por el
    // camino o se cierra la app, runUpdateCheck/destructor levantan el flag y
    // el bucle de descarga (SyncClient::httpDownloadToFile) se detiene en la
    // siguiente lectura.
    downloadStopRequested_.store(false);
    if (!httpDownloadToFile("/payload", installer, MAX_PAYLOAD_BYTES, &downloadStopRequested_)) {
        setStatus(downloadStopRequested_.load()
            ? juce::String::fromUTF8("Actualización cancelada: la descarga se detuvo (el móvil se fue o se está cerrando la app)")
            : juce::String::fromUTF8("Descarga fallida: vuelve a conectar el móvil"));
        tempDir.deleteRecursively();
        return;
    }

    // Verificación estricta antes de ejecutar: tamaño y SHA-256 del manifest
    if (info.sizeBytes > 0 && installer.getSize() != info.sizeBytes) {
        setStatus(juce::String::fromUTF8("Actualización cancelada: tamaño incorrecto"));
        tempDir.deleteRecursively();
        return;
    }
    const auto hash = juce::SHA256(installer).toHexString();
    if (hash.equalsIgnoreCase(info.sha256) == false) {
        setStatus(juce::String::fromUTF8("Actualización cancelada: la verificación SHA-256 no coincide"));
        tempDir.deleteRecursively();
        return;
    }

    // El SHA se ha verificado; el paso que queda (lanzar el instalador) es una
    // acción de sistema irreversible (pide UAC). Se le pregunta SIEMPRE al
    // usuario antes. El diálogo corre en el hilo de UI y el hilo de
    // actualización se queda a la espera hasta que el usuario responde (ver
    // confirmInstallOnUiThread).
    if (!confirmInstallOnUiThread(info.version)) {
        setStatus(juce::String::fromUTF8("Instalación cancelada por el usuario"));
        tempDir.deleteRecursively();
        return;
    }

    setStatus("Instalando " + info.version + juce::String::fromUTF8("… (Windows pedirá permiso)"));
    if (launchInstaller(installer))
        setStatus(juce::String::fromUTF8("Versión ") + info.version + juce::String::fromUTF8(" instalándose: Acoustical se reiniciará"));
    else
        setStatus("No se pudo iniciar el instalador (falta el permiso de administrador)");
}

// Pide al usuario, en el hilo de UI, si debe lanzarse el instalador.
//
// runUpdateCheck() corre en un hilo de red (updateThread_), NO en el de
// mensajes: un diálogo modal de JUCE solo puede mostrarse en el hilo de
// mensajes. Se encola el AlertWindow con callAsync (se ejecuta en el bucle de
// mensajes) y este hilo se queda a la espera de que el usuario pulse un botón.
// Es seguro: el destructor de PhoneLink hace join de updateThread_, así que
// este objeto no se destruye con el diálogo abierto; el promise vive en un
// shared_ptr que la lambda encolada captura por copia, de modo que no se
// destruye antes de tiempo ni al salir de este método. La espera acota el
// tiempo: si el diálogo no puede resolverse por alguna razón (p. ej. se está
// cerrando la app y el bucle de mensajes ya no corre) se trata como
// cancelación en lugar de bloquearse para siempre.
bool PhoneLink::confirmInstallOnUiThread(const juce::String& version) {
    auto result = std::make_shared<std::promise<int>>();
    auto future = result->get_future();
    MessageManager::callAsync([result, version] {
        auto* aw = new juce::AlertWindow(
            juce::String::fromUTF8("Actualizar Acoustical"),
            juce::String::fromUTF8("¿Instalar Acoustical ") + version
                + juce::String::fromUTF8(" descargada del móvil?\n\n"
                                         "Windows pedirá permiso de administrador (UAC)."),
            juce::MessageBoxIconType::QuestionIcon);
        aw->addButton(juce::String::fromUTF8("Instalar"), juce::Button::IDYes);
        aw->addButton(juce::String::fromUTF8("Cancelar"), juce::Button::IDNo);
        result->set_value(aw->runModal());
    });
    if (future.wait_for(std::chrono::minutes(2)) != std::future_status::ready)
        return false;   // sin respuesta a tiempo (p. ej. cierre de la app): no se instala
    try {
        return future.get() == juce::Button::IDYes;
    } catch (const std::future_error&) {
        return false;
    }
}
