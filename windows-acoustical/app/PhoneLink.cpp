#include "PhoneLink.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

void PhoneLink::pollDevices() {
    if (!adb_.existsAsFile()) {
        if (state_.load() != State::NoAdb) {
            state_.store(State::NoAdb);
            lastInfo_ = "adb no encontrado";
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
        lastInfo_ = "Esperando el móvil…";
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
        lastInfo_ = "Móvil conectado: " + serial;
        requestSync();
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
    char buffer[65536];
    const int n = socket.read(buffer, sizeof(buffer) - 1, false);
    socket.close();
    if (n <= 0) return false;
    buffer[n] = '\0';
    reply = juce::JSON::parse(juce::String::fromUTF8(buffer, n));
    return !reply.isVoid();
}

bool PhoneLink::pushSync(const juce::var& payload) {
    juce::var reply;
    auto msg = juce::DynamicObject::ObjectMap{};
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "push");
    obj->setProperty("payload", payload);
    if (!connectAndExchange(juce::var(obj), reply)) {
        state_.store(State::SyncError);
        return false;
    }
    lastInfo_ = "Ajustes enviados al móvil";
    return true;
}

bool PhoneLink::requestSync() {
    juce::var reply;
    auto obj = new juce::DynamicObject();
    obj->setProperty("type", "pull");
    if (!connectAndExchange(juce::var(obj), reply)) {
        state_.store(State::SyncError);
        lastInfo_ = "Abre Acoustical en el móvil para sincronizar";
        return false;
    }
    lastInfo_ = "Sincronizado con el móvil";
    if (onSync) onSync(reply);
    return true;
}
