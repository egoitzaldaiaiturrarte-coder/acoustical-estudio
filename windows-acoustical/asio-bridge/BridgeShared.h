// BridgeShared.h — protocolo de memoria compartida entre Acoustical Estudio
// (productor/consumidor) y el driver ASIO Acoustical Bridge. Un único bloque
// con anillos de audio float32 intercalados para reproducción (Cubase → app →
// tarjeta real) y captura (tarjeta/cable virtual → app → Cubase).
#pragma once
#include <cstdint>

namespace bridge {

constexpr unsigned kMagic = 0x41434252;   // 'ACBR'
constexpr unsigned kVersion = 1;
constexpr unsigned kChannels = 2;
constexpr unsigned kRingFrames = 16384;   // ~340 ms @48k
constexpr wchar_t kSharedMemoryName[] = L"AcousticalBridgeShared";
constexpr wchar_t kSharedEventName[] = L"AcousticalBridgeTick";

struct BridgeHeader {
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t sampleRate = 48000;
    uint32_t bufferSize = 512;
    uint32_t channels = kChannels;
    // Reproducción: la app escribe, el driver lee
    volatile uint64_t playbackWriteIndex = 0;   // en fotogramas
    volatile uint64_t playbackReadIndex = 0;
    // Captura: el driver escribe, la app lee
    volatile uint64_t captureWriteIndex = 0;
    volatile uint64_t captureReadIndex = 0;
    volatile uint32_t appRunning = 0;
};

// El bloque total = header + 2 anillos intercalados (playback y capture)
constexpr size_t kPlaybackBytes = kRingFrames * kChannels * sizeof(float);
constexpr size_t kSharedBlockSize = sizeof(BridgeHeader) + kPlaybackBytes * 2;

inline float* playbackRing(BridgeHeader* h) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(h) + sizeof(BridgeHeader));
}
inline float* captureRing(BridgeHeader* h) {
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(h) + sizeof(BridgeHeader) + kPlaybackBytes);
}

} // namespace bridge
