// BeaconListener.cpp
#include "BeaconListener.h"

namespace {
juce::int64 nowMs() { return juce::Time::getMillisecondCounter(); }
}

BeaconListener::BeaconListener(int port) : port_(port) {}

BeaconListener::~BeaconListener() { stop(); }

void BeaconListener::start(Callback cb) {
    if (running_.load()) return;
    cb_ = std::move(cb);
    running_.store(true);
    thread_ = std::thread([this] { run(); });
}

void BeaconListener::stop() {
    if (!running_.load() && !thread_.joinable()) return;
    running_.store(false);
    // El hilo está a lo sumo bloqueado en waitUntilReady(1000): sale en ≤ 1 s
    if (thread_.joinable()) thread_.join();
}

void BeaconListener::run() {
    juce::DatagramSocket socket(/*enableBroadcasting=*/true);
    if (!socket.bindToPort(port_)) {
        running_.store(false);   // el puerto estaba ocupado: deshabilitado
        return;
    }
    char buffer[512];
    while (running_.load()) {
        juce::String senderIp;
        int senderPort = 0;
        if (socket.waitUntilReady(true, 1000) != 1) continue;   // 0 = timeout
        const int n = socket.read(buffer, sizeof(buffer), false, senderIp, senderPort);
        if (n <= 0) continue;
        const auto v = juce::JSON::parse(juce::String::fromUTF8(buffer, n));
        auto* o = v.getDynamicObject();
        if (o == nullptr || o->getProperty("app").toString() != "acoustical") continue;
        {
            const std::lock_guard<std::mutex> lock(mtx_);
            ip_ = senderIp;
        }
        lastMs_.store(nowMs());
        if (cb_) cb_(senderIp, v);
    }
    // Abortar cualquier wait en curso antes de que el objeto se destruya
    socket.shutdown();
}

bool BeaconListener::isFresh(int maxAgeMs) const {
    const auto t = lastMs_.load();
    return t != 0 && (nowMs() - t) < maxAgeMs;
}

juce::String BeaconListener::lastIp() const {
    const std::lock_guard<std::mutex> lock(mtx_);
    return ip_;
}
