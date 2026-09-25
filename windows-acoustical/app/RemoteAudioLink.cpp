// RemoteAudioLink.cpp
#include "RemoteAudioLink.h"

#include <algorithm>
#include <cmath>

namespace {
juce::int64 nowMs() { return juce::Time::getMillisecondCounter(); }

unsigned readU16BE(const juce::uint8* p, int off) {
    return (static_cast<unsigned>(p[off]) << 8) | p[off + 1];
}
void writeU16BE(std::vector<juce::uint8>& p, int off, unsigned v) {
    p[static_cast<size_t>(off)] = static_cast<juce::uint8>((v >> 8) & 0xFF);
    p[static_cast<size_t>(off) + 1] = static_cast<juce::uint8>(v & 0xFF);
}
short clamp16(float v) {
    return static_cast<short>(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f);
}
}  // namespace

void RemoteAudioLink::start() {
    if (running_.load()) return;
    loadMicState();
    running_.store(true);
    thread_ = std::thread([this] { runReceive(); });
}

void RemoteAudioLink::stop() {
    if (!running_.load() && !thread_.joinable()) return;
    running_.store(false);
    if (thread_.joinable()) thread_.join();   // el hilo duerme a lo sumo 500 ms
}

RemoteAudioLink::~RemoteAudioLink() { stop(); }

void RemoteAudioLink::setCodeProvider(std::function<juce::String()> provider) {
    codeProvider_ = std::move(provider);
}

// ============================================================
// Registro de móviles (baliza)
// ============================================================

void RemoteAudioLink::noteBeacon(const juce::String& ip, const juce::String& id,
                                 const juce::String& name) {
    if (ip.isEmpty()) return;
    bool reSendMic = false;
    {
        const std::lock_guard<std::mutex> lock(mtx_);
        auto& st = states_[ip];
        if (st.dev.lastBeaconMs == 0) st.dev.ip = ip;
        if (!id.isEmpty()) st.dev.id = id;
        if (!name.isEmpty()) st.dev.name = name;
        st.dev.lastBeaconMs = nowMs();
        // Recobro automático: si el móvil estaba emitiendo antes de una
        // desconexión (Wi-Fi titubea, se reinicia la app…), re-pedimos su
        // micrófono al reaparecer. Es idempotente: el móvil ignora duplicados.
        reSendMic = !st.dev.micOn
            && std::find(micOnPersisted_.begin(), micOnPersisted_.end(), ip)
                   != micOnPersisted_.end();
        if (reSendMic) st.dev.micOn = true;
    }
    if (reSendMic) sendMicStart(ip);
}

std::vector<RemoteAudioLink::Device> RemoteAudioLink::devices() const {
    const std::lock_guard<std::mutex> lock(mtx_);
    std::vector<Device> out;
    out.reserve(states_.size());
    for (const auto& [ip, st] : states_) out.push_back(st.dev);
    return out;
}

std::vector<RemoteAudioLink::Device> RemoteAudioLink::liveDevices(int maxAgeMs) const {
    const auto all = devices();
    const auto now = nowMs();
    std::vector<Device> out;
    for (const auto& d : all)
        if (now - std::max(d.lastBeaconMs, d.lastAudioMs) < maxAgeMs)
            out.push_back(d);
    return out;
}

const RemoteAudioLink::Device* RemoteAudioLink::deviceByIp(const juce::String& ip) const {
    const std::lock_guard<std::mutex> lock(mtx_);
    auto it = states_.find(ip);
    return it == states_.end() ? nullptr : &it->second.dev;
}

bool RemoteAudioLink::isLive(const juce::String& ip, int maxAgeMs) const {
    if (auto* d = deviceByIp(ip))
        return nowMs() - std::max(d->lastBeaconMs, d->lastAudioMs) < maxAgeMs;
    return false;
}

juce::String RemoteAudioLink::findIpByName(const juce::String& name) const {
    const auto all = devices();
    for (const auto& d : all)
        if (displayName(d) == name) return d.ip;
    return {};
}

juce::String RemoteAudioLink::displayName(const Device& d) {
    return juce::String::fromUTF8("Móvil: ")
        + (d.name.isNotEmpty() ? d.name : d.ip)
        + juce::String::fromUTF8(" (") + d.ip + juce::String::fromUTF8(")");
}

// ============================================================
// Estado por dispositivo
// ============================================================

RemoteAudioLink::State* RemoteAudioLink::findState(const juce::String& ip) const {
    const std::lock_guard<std::mutex> lock(mtx_);
    auto it = states_.find(ip);
    return it == states_.end() ? nullptr : &it->second;
}

RemoteAudioLink::State* RemoteAudioLink::getOrCreate(const juce::String& ip) {
    const std::lock_guard<std::mutex> lock(mtx_);
    auto& st = states_[ip];
    if (st.dev.lastBeaconMs == 0) {
        st.dev.ip = ip;
        if (st.dev.name.isEmpty())
            st.dev.name = juce::String::fromUTF8("Móvil ") + ip;
    }
    return &st;
}

// ============================================================
// Hilo de recepción: tramas de micrófono de los móviles (41044)
// ============================================================

void RemoteAudioLink::runReceive() {
    juce::DatagramSocket socket;
    if (!socket.bindToPort(MIC_PORT)) {
        running_.store(false);   // el puerto estaba ocupado: deshabilitado
        return;
    }
    std::vector<juce::uint8> buffer(8192);
    while (running_.load()) {
        juce::String senderIp;
        int senderPort = 0;
        if (socket.waitUntilReady(true, 500) != 1) continue;   // 0 = timeout
        const int n = socket.read(buffer.data(), static_cast<int>(buffer.size()),
                                  false, senderIp, senderPort);
        if (n < 10) continue;
        const auto* d = buffer.data();
        if (d[0] == '{') continue;   // JSON (control móvil→PC; hoy no se usa)

        const unsigned rate = readU16BE(d, 2);
        const unsigned ch = readU16BE(d, 4);
        if (rate < 8000 || rate > 96000 || (ch != 1 && ch != 2)) continue;
        const int samples = (n - 8) / (2 * static_cast<int>(ch));
        if (samples <= 0 || samples > 4096) continue;

        // PCM16 LE intercalado → mono float (-1…1) + nivel pico
        std::vector<float> mono(static_cast<size_t>(samples));
        float peak = 0.0f;
        for (int i = 0; i < samples; ++i) {
            float v = 0.0f;
            for (unsigned c = 0; c < ch; ++c) {
                const int o = 8 + (i * static_cast<int>(ch) + static_cast<int>(c)) * 2;
                const short s16 = static_cast<short>(static_cast<unsigned>(
                    d[static_cast<size_t>(o)] | (d[static_cast<size_t>(o) + 1] << 8)));
                v += static_cast<float>(s16) / 32768.0f;
            }
            v /= static_cast<float>(ch);
            mono[static_cast<size_t>(i)] = v;
            peak = std::max(peak, std::fabs(v));
        }

        auto* ds = getOrCreate(senderIp);
        ds->ring.write(mono.data(), samples);
        ds->dev.lastAudioMs = nowMs();
        ds->levelDb.store(peak > 1e-5f ? 20.0f * std::log10(peak) : -120.0f);
    }
    // Abortar cualquier wait en curso antes de que el objeto se destruya
    socket.shutdown();
}

// ============================================================
// M2: pedir/parar el micrófono del móvil + mezcla en el callback de audio
// ============================================================

bool RemoteAudioLink::setMicOn(const juce::String& ip, bool on) {
    State* ds = nullptr;
    {
        const std::lock_guard<std::mutex> lock(mtx_);
        auto it = states_.find(ip);
        if (it == states_.end()) return false;
        ds = &it->second;
        if (ds->dev.micOn == on) return true;
        ds->dev.micOn = on;
    }
    if (on) sendMicStart(ip);
    else sendControl(ip, "{\"cmd\":\"mic_stop\"}");
    saveMicState();
    return true;
}

bool RemoteAudioLink::micOn(const juce::String& ip) const {
    if (auto* d = deviceByIp(ip)) return d->micOn;
    return false;
}

void RemoteAudioLink::setMicGain(const juce::String& ip, float g) {
    if (auto* ds = findState(ip))
        ds->gain.store(std::max(0.0f, std::min(2.0f, g)), std::memory_order_relaxed);
}

float RemoteAudioLink::micGain(const juce::String& ip) const {
    if (auto* ds = findState(ip)) return ds->gain.load(std::memory_order_relaxed);
    return 0.0f;
}

float RemoteAudioLink::micLevelDb(const juce::String& ip) const {
    if (auto* ds = findState(ip)) return ds->levelDb.load(std::memory_order_relaxed);
    return -120.0f;
}

std::vector<juce::String> RemoteAudioLink::activeMicIps() const {
    const std::lock_guard<std::mutex> lock(mtx_);
    std::vector<juce::String> out;
    for (const auto& [ip, st] : states_)
        if (st.dev.micOn) out.push_back(ip);
    return out;
}

void RemoteAudioLink::pullMic(const juce::String& ip, float* dest, int n, double outRate) {
    if (dest == nullptr || n <= 0) return;
    std::fill_n(dest, n, 0.0f);
    auto* ds = findState(ip);
    if (ds == nullptr) return;
    auto& ring = ds->ring;
    const int avail = ring.buffered();
    if (avail < 2) return;

    // El móvil manda a 48 kHz; resampleamos a la tasa del dispositivo del PC
    // con interpolación lineal sobre las ÚLTIMAS muestras del anillo (el
    // anillo se autolimita: siempre hay ~340 ms de señal fresca).
    const double ratio = 48000.0 / (outRate > 8000.0 ? outRate : 48000.0);
    double pos = ds->micPos;
    const int needed = static_cast<int>(pos + (double)n * ratio) + 1;
    if (avail < needed) pos = 0.0;      // underrun: resincronizar
    const int take = std::min(needed, avail);
    const std::int64_t base = ring.head() - take;

    for (int s = 0; s < n; ++s) {
        const double c = pos + (double)s * ratio;
        const int i0 = static_cast<int>(c);
        if (i0 < take - 1) {
            const float frac = static_cast<float>(c - std::floor(c));
            dest[s] = ring.at(base + i0) * (1.0f - frac)
                    + ring.at(base + i0 + 1) * frac;
        } else if (i0 < take) {
            dest[s] = ring.at(base + i0);
        }
    }
    const double total = pos + (double)n * ratio;
    ds->micPos = total - std::floor(total);
}

void RemoteAudioLink::sendMicStart(const juce::String& ip) {
    auto* o = new juce::DynamicObject();
    o->setProperty("cmd", "mic_start");
    o->setProperty("pcIp", ownLanIp());
    o->setProperty("port", (double)MIC_PORT);
    o->setProperty("rate", 48000.0);
    o->setProperty("code", codeProvider_ ? codeProvider_() : juce::String());
    sendControl(ip, juce::JSON::toString(juce::var(o)));
}

void RemoteAudioLink::sendControl(const juce::String& ip, const juce::String& json) {
    const std::lock_guard<std::mutex> lock(sendLock_);
    sendSock_.write(ip, PLAY_PORT, json.toRawUTF8(),
                    static_cast<int>(json.getNumBytesAsUTF8()));
}

// ============================================================
// M3: audio a los altavoces del móvil (lo llama la ruta de red del Hub)
// ============================================================

void RemoteAudioLink::sendToPhone(const juce::String& ip, const float* L,
                                  const float* R, int n, double rate) {
    if (L == nullptr || n <= 0) return;
    auto* ds = findState(ip);
    if (ds == nullptr) return;
    const int ch = (R != nullptr) ? 2 : 1;
    std::vector<juce::uint8> bytes(8 + static_cast<size_t>(n) * ch * 2);
    writeU16BE(bytes, 0, ds->seqTx.fetch_add(1, std::memory_order_relaxed));
    writeU16BE(bytes, 2, static_cast<unsigned>(std::lround(rate)));
    writeU16BE(bytes, 4, static_cast<unsigned>(ch));
    writeU16BE(bytes, 6, 0);
    for (int i = 0; i < n; ++i) {
        const int o = 8 + i * ch * 2;
        const short sl = clamp16(L[i]);
        bytes[static_cast<size_t>(o)] = static_cast<juce::uint8>(sl & 0xFF);
        bytes[static_cast<size_t>(o) + 1] = static_cast<juce::uint8>((sl >> 8) & 0xFF);
        if (ch == 2) {
            const short sr = clamp16(R[i]);
            bytes[static_cast<size_t>(o) + 2] = static_cast<juce::uint8>(sr & 0xFF);
            bytes[static_cast<size_t>(o) + 3] = static_cast<juce::uint8>((sr >> 8) & 0xFF);
        }
    }
    const std::lock_guard<std::mutex> lock(sendLock_);
    sendSock_.write(ip, PLAY_PORT, bytes.data(), static_cast<int>(bytes.size()));
}

// ============================================================
// Varios
// ============================================================

juce::String RemoteAudioLink::ownLanIp() {
    const auto addrs = juce::IPAddress::getAllAddresses(false);
    for (const auto& a : addrs) {
        const auto s = a.toString();
        if (s.startsWith("127.")) continue;
        return s;
    }
    return {};
}

juce::File RemoteAudioLink::stateFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Acoustical").getChildFile("remoteaudio.json");
}

void RemoteAudioLink::loadMicState() {
    const auto f = stateFile();
    if (!f.existsAsFile()) return;
    // `parsed` debe vivir todo el bloque: getDynamicObject() apunta a su interior
    const juce::var parsed = juce::JSON::parse(f.loadFileAsString());
    if (auto* o = parsed.getDynamicObject()) {
        const auto v = o->getProperty("micOn");
        if (v.isArray())
            for (int i = 0; i < v.size(); ++i) {
                const auto s = v[i].toString();
                if (!s.isEmpty()) micOnPersisted_.push_back(s);
            }
    }
}

void RemoteAudioLink::saveMicState() {
    std::vector<juce::String> ips;
    {
        const std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& [ip, st] : states_)
            if (st.dev.micOn) ips.push_back(ip);
    }
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& ip : ips) arr.add(juce::var(ip));
    o->setProperty("micOn", juce::var(arr));
    stateFile().getParentDirectory().createDirectory();
    stateFile().replaceWithText(juce::JSON::toString(juce::var(o), true));
}
