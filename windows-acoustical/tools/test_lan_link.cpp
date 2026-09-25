// test_lan_link.cpp — Protocolo del enlace móvil↔PC validado de verdad:
// un "móvil falso" (servidor TCP + baliza UDP en loopback) implementa el
// mismo contrato que PhoneSyncManager.kt, y SyncClient/BeaconListener —
// los MISMOS .cpp que enlaza la app de Windows — negocian con él:
//   1. descubrimiento por baliza UDP (IP del "móvil")
//   2. emparejamiento: sin código / código malo → rechazo "pair"
//   3. sync pull/push con código bueno
//   4. HTTP: /sync protegido, /manifest abierto, /payload + SHA-256
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../app/BeaconListener.h"
#include "../app/SyncClient.h"
#include "../app/RemoteAudioLink.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_checks = 0, g_fails = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
    else std::printf("ok:   %s\n", what);
}

// ============================================================
// "Móvil falso": servidor TCP 127.0.0.1:kTcp + baliza UDP a kUdp
// ============================================================
class FakePhone {
public:
    FakePhone(int tcpPort, int udpPort, juce::String code)
        : port_(tcpPort), udpPort_(udpPort), code_(std::move(code)) {
        // "Instalador" de prueba: 128 KB con patrón, SHA calculado
        payload_.resize(128 * 1024);
        for (size_t i = 0; i < payload_.size(); ++i) payload_[i] = static_cast<char>(i * 7 % 251);
        payloadSha_ = juce::SHA256(reinterpret_cast<const juce::uint8*>(payload_.data()), payload_.size()).toHexString();
    }

    void start() {
        server_ = std::thread([this] { serverLoop(); });
        beacon_ = std::thread([this] { beaconLoop(); });
        for (int i = 0; i < 100 && !listening_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void stop() {
        running_.store(false);
        // Despierta el accept() bloqueado con una conexión dummy
        juce::StreamingSocket dummy;
        dummy.connect("127.0.0.1", port_, 500);
        dummy.close();
        if (server_.joinable()) server_.join();
        if (beacon_.joinable()) beacon_.join();
    }

    juce::String manifestJson() const {
        return juce::String(
            "{\"type\":\"manifest\",\"ok\":true,\"windows\":{"
            "\"version\":\"9.9.9\",\"sha256\":\"" + payloadSha_ +
            "\",\"size\":" + juce::String(static_cast<int>(payload_.size())) +
            ",\"hasPayload\":true}}");
    }

    juce::int64 payloadSize() const { return static_cast<juce::int64>(payload_.size()); }
    juce::String payloadSha() const { return payloadSha_; }

    // --- M1: ajustes en ambos sentidos ---

    /** Simula el botón "Enviar mis ajustes al PC" de la app real. */
    void stageSendToPc() { sendToPc_ = true; }
    bool isStaged() const { return sendToPc_; }
    /** La última config que el PC le empujó (su botón "Sincronizar"). */
    juce::var lastReceivedConfig() const { return lastReceivedConfig_; }

    /** Config "en vivo" del móvil: igual forma que la que publica el ViewModel. */
    static juce::var fakeLocalConfig() {
        auto* c = new juce::DynamicObject();
        c->setProperty("maxGainDb", 14.0);
        c->setProperty("smoothingFactor", 0.35);
        c->setProperty("noiseFloorDb", -120.0);
        c->setProperty("noiseSubtractionEnabled", true);
        c->setProperty("correctionEnabled", false);
        c->setProperty("targetSpl", 80.0);
        c->setProperty("audioDelayMs", 30.0);
        auto* eqs = new juce::DynamicObject();
        auto* eq1 = new juce::DynamicObject();
        eq1->setProperty("enabled", true);
        eq1->setProperty("intervalMs", 600);
        eq1->setProperty("maxGainDb", 15.0);
        eq1->setProperty("mixerLevel", 0.7);
        eq1->setProperty("speedMultiplier", 1.5);
        eq1->setProperty("extraSweeps", 2);
        eqs->setProperty("eq1", juce::var(eq1));
        c->setProperty("dynamicEqs", juce::var(eqs));
        return juce::var(c);
    }

    /** La respuesta de sync: estado guardado + config en vivo (+ bandera staged). */
    juce::var currentSyncJson() const {
        auto* payload = new juce::DynamicObject();
        if (auto* s = stored_.getDynamicObject())
            for (const auto& p : s->getProperties()) payload->setProperty(p.name, p.value);
        if (sendToPc_) payload->setProperty("sendToPc", true);
        payload->setProperty("config", fakeLocalConfig());
        auto* o = new juce::DynamicObject();
        o->setProperty("type", "sync");
        o->setProperty("ok", true);
        o->setProperty("payload", juce::var(payload));
        return juce::var(o);
    }

private:
    void serverLoop() {
        juce::StreamingSocket listener;
        if (!listener.createListener(port_, "127.0.0.1")) return;
        listening_.store(true);
        while (running_) {
            auto* client = listener.waitForNextConnection();
            if (client == nullptr) break;
            handleClient(client);
            delete client;
            if (!running_) break;
        }
    }

    static bool isHttpComplete(const juce::MemoryBlock& raw) {
        const int sz = static_cast<int>(raw.getSize());
        if (sz < 5) return false;
        const char* p = static_cast<const char*>(raw.getData());
        for (int i = 0; i + 3 < sz; ++i)
            if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') return true;
        return false;
    }

    void handleClient(juce::StreamingSocket* s) {
        char buffer[65536];
        juce::MemoryBlock raw;
        // El cliente (como PhoneLink) no cierra la escritura: el HTTP termina
        // en \r\n\r\n; el JSON simple, en ~600 ms de silencio (como el móvil real)
        for (int i = 0; i < 80; ++i) {
            if (s->waitUntilReady(true, 150) <= 0) {
                if (isHttpComplete(raw)) break;
                if (i >= 4 && raw.getSize() > 0) break;
                if (i >= 40) break;
                continue;
            }
            const int n = s->read(buffer, sizeof(buffer), false);
            if (n <= 0) break;
            raw.append(buffer, static_cast<size_t>(n));
            if (isHttpComplete(raw)) break;
        }
        const auto text = juce::String::fromUTF8(
            static_cast<const char*>(raw.getData()), static_cast<int>(raw.getSize())).trim();
        if (text.startsWith("GET ") || text.startsWith("POST "))
            handleHttp(s, text);
        else if (text.isNotEmpty())
            handleJson(s, text);
        s->close();
    }

    bool codeOk(const juce::var& v) const {
        if (auto* o = v.getDynamicObject())
            return o->getProperty("code").toString() == code_;
        return false;
    }

    void handleJson(juce::StreamingSocket* s, const juce::String& text) {
        const auto v = juce::JSON::parse(text);
        if (!codeOk(v)) {
            write(s, "{\"type\":\"pair\",\"ok\":false,\"error\":\"code\"}");
            return;
        }
        auto* o = v.getDynamicObject();
        const auto type = o ? o->getProperty("type").toString() : juce::String();
        if (type != "push" && type != "pull") return;
        if (type == "push") {
            // Mismo comportamiento que PhoneSyncManager.storeSyncPayload:
            // "acked" → borra el estado pendiente; lo demás → se guarda y
            // (si lleva config) queda como última config recibida del PC.
            if (auto* pl = o->getProperty("payload").getDynamicObject()) {
                if (static_cast<bool>(pl->getProperty("acked")) &&
                    !static_cast<bool>(pl->getProperty("sendToPc"))) {
                    sendToPc_ = false;
                    stored_ = juce::var();
                } else {
                    stored_ = juce::var(pl);
                    if (auto* c = pl->getProperty("config").getDynamicObject())
                        lastReceivedConfig_ = juce::var(c);
                }
            }
        }
        write(s, juce::JSON::toString(currentSyncJson(), false));
    }

    void handleHttp(juce::StreamingSocket* s, const juce::String& text) {
        const auto line = text.upToFirstOccurrenceOf("\r\n", false, false);
        auto parts = juce::StringArray::fromTokens(line, " ");
        const auto path = parts.size() > 1 ? parts[1].upToFirstOccurrenceOf("?", 0, false) : juce::String();
        juce::String codeHeader;
        for (const auto& h : juce::StringArray::fromLines(text))
            if (h.startsWithIgnoreCase("X-Acoustical-Code:"))
                codeHeader = h.fromFirstOccurrenceOf(":", false, false).trim();

        if (path == "/sync" && codeHeader != code_) {
            write(s, "HTTP/1.0 403 Forbidden\r\nContent-Type: application/json\r\n"
                     "Content-Length: 44\r\nConnection: close\r\n\r\n"
                     "{\"type\":\"pair\",\"ok\":false,\"error\":\"code\"}");
            return;
        }
        if (path == "/sync") {
            const juce::String body = juce::JSON::toString(currentSyncJson(), false);
            write(s, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + juce::String(body.length()) +
                     "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        if (path == "/manifest") {
            const juce::String body = manifestJson();
            write(s, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                     "Content-Length: " + juce::String(body.length()) +
                     "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        if (path == "/payload") {
            write(s, "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n"
                     "Content-Length: " + juce::String(static_cast<int>(payload_.size())) +
                     "\r\nConnection: close\r\n\r\n");
            s->write(payload_.data(), static_cast<int>(payload_.size()));
            return;
        }
        write(s, "HTTP/1.0 404 Not Found\r\nContent-Type: application/json\r\n"
                 "Content-Length: 15\r\nConnection: close\r\n\r\n"
                 "{\"ok\":false}\r\n");
    }

    void write(juce::StreamingSocket* s, const juce::String& t) {
        const auto u = t.toUTF8();
        s->write(u.getAddress(), static_cast<int>(u.sizeInBytes() - 1));
    }

    void beaconLoop() {
        juce::DatagramSocket sock(false);
        const juce::String msg =
            "{\"app\":\"acoustical\",\"port\":41041,\"ver\":\"test\",\"dev\":\"fake\"}";
        while (running_) {
            const auto u = msg.toUTF8();
            sock.write("127.0.0.1", udpPort_, u.getAddress(), static_cast<int>(u.sizeInBytes() - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    int port_, udpPort_;
    juce::String code_;
    std::vector<char> payload_;
    juce::String payloadSha_;
    juce::var stored_;              // último push del PC (o estado staged)
    bool sendToPc_ = false;         // "Enviar mis ajustes al PC" encolado
    juce::var lastReceivedConfig_;  // última config empujada por el PC
    std::atomic<bool> running_{true}, listening_{false};
    std::thread server_, beacon_;
};

juce::var pullMsg() {
    auto* o = new juce::DynamicObject();
    o->setProperty("type", "pull");
    return juce::var(o);
}

juce::DynamicObject* payloadOf(const juce::var& reply) {
    if (auto* o = reply.getDynamicObject())
        if (auto* p = o->getProperty("payload").getDynamicObject()) return p;
    return nullptr;
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    constexpr int kTcp = 42041, kUdp = 42042;
    const juce::String kCode("123456");

    FakePhone phone(kTcp, kUdp, kCode);
    phone.start();
    if (!phone.payloadSha().isNotEmpty()) { std::printf("FAIL: móvil falso no arrancó\n"); return 1; }

    // 1) Baliza: BeaconListener descubre la "IP" del móvil
    juce::String seenIp;
    BeaconListener beacon(kUdp);
    beacon.start([&](const juce::String& ip, const juce::var&) { seenIp = ip; });
    for (int i = 0; i < 100 && seenIp.isEmpty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(seenIp == "127.0.0.1", "baliza UDP descubierta (IP del móvil)");
    check(beacon.isFresh(5000) && beacon.lastIp() == seenIp, "BeaconListener::isFresh/lastIp");

    juce::var reply;

    // 2) Sin código ni con código malo → rechazo "pair"
    check(SyncClient::exchange("127.0.0.1", kTcp, pullMsg(), reply, ""),
          "exchange sin código: el móvil respondió");
    check(reply.getDynamicObject() != nullptr &&
              reply.getDynamicObject()->getProperty("type").toString() == "pair",
          "sin código → respuesta 'pair' (rechazo)");
    check(SyncClient::exchange("127.0.0.1", kTcp, pullMsg(), reply, "999999") &&
              reply.getDynamicObject()->getProperty("type").toString() == "pair",
          "código malo → respuesta 'pair'");

    // 3) Con código bueno → sync ok
    check(SyncClient::exchange("127.0.0.1", kTcp, pullMsg(), reply, kCode) &&
              reply.getDynamicObject() != nullptr &&
              reply.getDynamicObject()->getProperty("type").toString() == "sync" &&
              static_cast<bool>(reply.getDynamicObject()->getProperty("ok")),
          "código bueno → sync ok");

    // 4) Push: el "móvil" devuelve el payload que le envió el PC
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("type", "push");
        auto* pl = new juce::DynamicObject();
        pl->setProperty("hello", "world");
        o->setProperty("payload", juce::var(pl));
        check(SyncClient::exchange("127.0.0.1", kTcp, juce::var(o), reply, kCode) &&
                  reply.getDynamicObject() != nullptr &&
                  reply.getDynamicObject()->getProperty("payload").getDynamicObject() != nullptr &&
                  reply.getDynamicObject()->getProperty("payload").getDynamicObject()
                      ->getProperty("hello").toString() == "world",
              "push → el móvil devolvió el payload del PC");
    }

    // 4b) Ajustes en ambos sentidos (M1): la config en vivo del móvil viaja en
    //     cada respuesta; "Enviar mis ajustes al PC" (staged) + confirmación
    //     "acked"; el push del PC llega íntegro al móvil.
    {
        // pull → config en vivo del móvil
        check(SyncClient::exchange("127.0.0.1", kTcp, pullMsg(), reply, kCode) &&
                  payloadOf(reply) != nullptr &&
                  payloadOf(reply)->getProperty("config").getDynamicObject() != nullptr &&
                  static_cast<double>(payloadOf(reply)->getProperty("config").getDynamicObject()
                      ->getProperty("maxGainDb")) == 14.0 &&
                  payloadOf(reply)->getProperty("config").getDynamicObject()->getProperty("dynamicEqs")
                      .getDynamicObject() != nullptr,
              "pull → config en vivo del móvil (escalares + ecuas)");

        // El móvil encola "enviar mis ajustes al PC" → el sondeo del PC ve la bandera
        phone.stageSendToPc();
        check(SyncClient::exchange("127.0.0.1", kTcp, pullMsg(), reply, kCode) &&
                  static_cast<bool>(payloadOf(reply)->getProperty("sendToPc")),
              "staged → el sondeo del PC ve sendToPc=true");

        // El PC confirma con "acked" → el móvil borra el estado pendiente
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("type", "push");
            auto* pl = new juce::DynamicObject();
            pl->setProperty("acked", true);
            o->setProperty("payload", juce::var(pl));
            check(SyncClient::exchange("127.0.0.1", kTcp, juce::var(o), reply, kCode) &&
                      !phone.isStaged() &&
                      !static_cast<bool>(payloadOf(reply)->getProperty("sendToPc")),
                  "acked → el móvil borró el estado pendiente");
        }

        // El PC empuja su config completa (botón "Sincronizar") → el móvil la recibe
        {
            auto* c = new juce::DynamicObject();
            c->setProperty("maxGainDb", 12.0);
            c->setProperty("smoothingFactor", 0.3);
            c->setProperty("correctionEnabled", true);
            c->setProperty("targetSpl", 75.0);
            c->setProperty("audioDelayMs", 25.0);
            c->setProperty("noiseFloorDb", -120.0);
            c->setProperty("noiseSubtractionEnabled", true);
            auto* eqs = new juce::DynamicObject();
            auto* eq1 = new juce::DynamicObject();
            eq1->setProperty("enabled", true);
            eq1->setProperty("intervalMs", 800);
            eq1->setProperty("maxGainDb", 12.0);
            eq1->setProperty("mixerLevel", 0.8);
            eq1->setProperty("speedMultiplier", 1.0);
            eq1->setProperty("extraSweeps", 1);
            eqs->setProperty("eq1", juce::var(eq1));
            c->setProperty("dynamicEqs", juce::var(eqs));
            auto* eqG = new juce::DynamicObject();
            eqG->setProperty("band0", 1.5);
            c->setProperty("eqGains", juce::var(eqG));

            auto* o = new juce::DynamicObject();
            o->setProperty("type", "push");
            auto* pl = new juce::DynamicObject();
            pl->setProperty("config", juce::var(c));
            o->setProperty("payload", juce::var(pl));
            check(SyncClient::exchange("127.0.0.1", kTcp, juce::var(o), reply, kCode) &&
                      phone.lastReceivedConfig().getDynamicObject() != nullptr &&
                      static_cast<double>(phone.lastReceivedConfig().getDynamicObject()
                          ->getProperty("maxGainDb")) == 12.0 &&
                      phone.lastReceivedConfig().getDynamicObject()->getProperty("dynamicEqs")
                          .getDynamicObject() != nullptr &&
                      phone.lastReceivedConfig().getDynamicObject()->getProperty("eqGains")
                          .getDynamicObject() != nullptr,
                  "push del PC → el móvil guardó la config completa (escalares, ecuas, curva)");
        }
    }

    // 5) HTTP: /sync exige código, /manifest está abierto
    // (httpGet devuelve las cabeceras; el cuerpo va en `body`)
    auto bodyText = [](const juce::MemoryBlock& b) {
        return juce::String::fromUTF8(static_cast<const char*>(b.getData()),
                                      static_cast<int>(b.getSize()));
    };
    juce::MemoryBlock body;
    check(SyncClient::httpGet("127.0.0.1", kTcp, "/sync", body, "").isEmpty(),
          "GET /sync sin código → 403");
    check(!SyncClient::httpGet("127.0.0.1", kTcp, "/manifest", body, kCode).isEmpty()
              && bodyText(body).contains("windows"),
          "GET /manifest → manifest JSON");
    {
        const bool got = !SyncClient::httpGet("127.0.0.1", kTcp, "/sync", body, kCode).isEmpty();
        juce::var parsed = got ? juce::JSON::parse(bodyText(body)) : juce::var();
        auto* pobj = parsed.getDynamicObject();
        check(got && pobj != nullptr && static_cast<bool>(pobj->getProperty("ok"))
                  && payloadOf(parsed) != nullptr,
              "GET /sync con código → ok (JSON válido con payload)");
    }

    // 6) Descarga del "instalador" + verificación SHA-256 (flujo de actualización)
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("acoustical_lantest_payload.bin");
    const bool dl = SyncClient::httpDownloadToFile("127.0.0.1", kTcp, "/payload", tmp, kCode, 1 << 20);
    check(dl && tmp.existsAsFile() && tmp.getSize() == phone.payloadSize(),
          "GET /payload → descarga completa (128 KB)");
    check(dl && juce::SHA256(tmp).toHexString() == phone.payloadSha(),
          "SHA-256 del payload descargado coincide");
    tmp.deleteFile();

    // 7) Puente de audio Wi-Fi (M2/M3): lazo UDP en loopback con un
    //    "móvil falso". El móvil manda su micro (seno 440 Hz, 48 kHz, mono)
    //    al puerto 41044 del PC y escucha en 41043 el control y la reproducción.
    {
        // Estado persistente de una ejecución anterior: borrar para que el
        // test sea determinista (no re-pida micro al arrancar).
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Acoustical").getChildFile("remoteaudio.json").deleteFile();

        RemoteAudioLink link;
        link.setCodeProvider([&] { return kCode; });
        link.start();
        link.noteBeacon("127.0.0.1", "test-id", "MóvilTest");
        check(link.deviceByIp("127.0.0.1") != nullptr, "baliza → móvil en el registro");
        check(link.liveDevices().size() == 1, "liveDevices → 1 móvil en vivo");

        // "Móvil" que recibe control y audio (puerto 41043)
        juce::DatagramSocket phoneRx;
        check(phoneRx.bindToPort(RemoteAudioLink::PLAY_PORT),
              "móvil falso: oyente 41043 (control/reproducción)");

        // "Móvil" que emite su micro (al puerto 41044)
        std::atomic<bool> stopPhone{false};
        std::thread phoneMic([&] {
            juce::DatagramSocket s;
            unsigned seq = 0;
            double phase = 0.0;
            while (!stopPhone.load()) {
                const int n = 480;   // 10 ms a 48 kHz
                std::vector<juce::uint8> bytes(8 + static_cast<size_t>(n) * 2);
                bytes[0] = static_cast<juce::uint8>((seq >> 8) & 0xFF);
                bytes[1] = static_cast<juce::uint8>(seq & 0xFF);
                bytes[2] = static_cast<juce::uint8>((48000 >> 8) & 0xFF);
                bytes[3] = static_cast<juce::uint8>(48000 & 0xFF);
                bytes[4] = 0; bytes[5] = 1;   // ch=1 en big-endian
                bytes[6] = 0; bytes[7] = 0;
                for (int i = 0; i < n; ++i) {
                    const short v = static_cast<short>(0.5 * 32767.0 * sin(phase));
                    phase += 2.0 * M_PI * 440.0 / 48000.0;
                    bytes[8 + 2 * i] = static_cast<juce::uint8>(v & 0xFF);
                    bytes[9 + 2 * i] = static_cast<juce::uint8>((v >> 8) & 0xFF);
                }
                s.write("127.0.0.1", RemoteAudioLink::MIC_PORT, bytes.data(),
                        static_cast<int>(bytes.size()));
                seq = (seq + 1) & 0xFFFF;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        // M2: las tramas llegan y el nivel se mide
        for (int i = 0; i < 100 && link.micLevelDb("127.0.0.1") < -60.0f; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(link.micLevelDb("127.0.0.1") > -30.0f,
              "M2: tramas del micro llegan al PC (nivel > -30 dB)");

        // M2: pullMic devuelve el seno resampleado
        {
            std::vector<float> block(480);
            double rmsSum = 0.0;
            int zeros = 0;
            for (int b = 0; b < 5; ++b) {
                link.pullMic("127.0.0.1", block.data(), 480, 48000.0);
                for (int s = 1; s < 480; ++s) {
                    rmsSum += block[static_cast<size_t>(s)] * block[static_cast<size_t>(s)];
                    if (b > 0 && (block[static_cast<size_t>(s)] >= 0.0f)
                                != (block[static_cast<size_t>(s - 1)] >= 0.0f))
                        ++zeros;
                }
            }
            const double rms = std::sqrt(rmsSum / (5.0 * 479.0));
            check(rms > 0.1 && rms < 0.4, "M2: pullMic resamplea el micro (RMS del seno 0,5)");
            // 440 Hz en ~40 ms → ~18 ciclos → ~36 cruces de cero (margen amplio)
            check(zeros > 15 && zeros < 70, "M2: la frecuencia llega correcta (~440 Hz)");
        }

        // M2: pedir el micro → el "móvil" recibe mic_start con el código
        check(link.setMicOn("127.0.0.1", true), "M2: setMicOn(true) aceptado");
        check(link.micOn("127.0.0.1"), "M2: estado micOn guardado");
        {
            juce::String senderIp;
            int senderPort = 0;
            char buf[512];
            juce::String got;
            for (int i = 0; i < 100 && got.isEmpty(); ++i) {
                if (phoneRx.waitUntilReady(true, 100) == 1) {
                    const int n = phoneRx.read(buf, sizeof(buf), false, senderIp, senderPort);
                    if (n > 0) got = juce::String::fromUTF8(buf, n);
                }
            }
            check(got.contains("mic_start"), "M2: el móvil recibió mic_start (UDP 41043)");
            check(got.contains(kCode.toRawUTF8()), "M2: mic_start lleva el código de emparejamiento");
            check(got.contains("41044"), "M2: mic_start lleva el puerto de destino (41044)");
        }

        // M3: sendToPhone → el "móvil" recibe la trama de audio
        {
            std::vector<float> L(480), R(480);
            double phase = 0.0;
            for (int i = 0; i < 480; ++i) {
                L[static_cast<size_t>(i)] = static_cast<float>(0.5 * sin(phase));
                R[static_cast<size_t>(i)] = static_cast<float>(0.25 * sin(phase));
                phase += 2.0 * M_PI * 440.0 / 48000.0;
            }
            link.sendToPhone("127.0.0.1", L.data(), R.data(), 480, 48000.0);
            juce::String senderIp;
            int senderPort = 0;
            char buf[2048];
            int n = 0;
            for (int i = 0; i < 100 && n < 10; ++i) {
                if (phoneRx.waitUntilReady(true, 100) == 1) {
                    n = phoneRx.read(buf, sizeof(buf), false, senderIp, senderPort);
                    if (n > 0) break;
                }
            }
            check(n == 8 + 480 * 2 * 2, "M3: el móvil recibió la trama (1928 bytes)");
            if (n >= 8 + 4) {
                const auto* d = reinterpret_cast<const juce::uint8*>(buf);
                const unsigned rate = (static_cast<unsigned>(d[2]) << 8) | d[3];
                const unsigned ch = (static_cast<unsigned>(d[4]) << 8) | d[5];
                check(rate == 48000 && ch == 2, "M3: cabecera (rate=48000, ch=2)");
                int maxL = 0, maxR = 0;
                for (int i = 0; i < 480; ++i) {
                    const short l = static_cast<short>(
                        d[8 + i * 4] | (d[9 + i * 4] << 8));
                    const short r = static_cast<short>(
                        d[10 + i * 4] | (d[11 + i * 4] << 8));
                    maxL = std::max(maxL, std::abs(static_cast<int>(l)));
                    maxR = std::max(maxR, std::abs(static_cast<int>(r)));
                }
                check(maxL > 15000 && maxL < 17500, "M3: canal L en PCM16 (amplitud ≈ 0,5)");
                check(maxR > 7000 && maxR < 9500, "M3: canal R en PCM16 (amplitud ≈ 0,25)");
            }
        }

        // M2: parar el micro → el "móvil" recibe mic_stop
        check(link.setMicOn("127.0.0.1", false), "M2: setMicOn(false) aceptado");
        {
            juce::String senderIp;
            int senderPort = 0;
            char buf[512];
            juce::String got;
            for (int i = 0; i < 100 && got.isEmpty(); ++i) {
                if (phoneRx.waitUntilReady(true, 100) == 1) {
                    const int n = phoneRx.read(buf, sizeof(buf), false, senderIp, senderPort);
                    if (n > 0) {
                        got = juce::String::fromUTF8(buf, n);
                        if (got.contains("mic_stop")) break;
                    }
                }
            }
            check(got.contains("mic_stop"), "M2: el móvil recibió mic_stop");
        }

        stopPhone.store(true);
        phoneMic.join();
        phoneRx.shutdown();
        link.stop();
    }

    beacon.stop();
    phone.stop();

    std::printf("\n%d comprobaciones, %d fallos\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
