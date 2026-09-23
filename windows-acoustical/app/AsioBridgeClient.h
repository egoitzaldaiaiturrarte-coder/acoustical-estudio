// AsioBridgeClient.h — lado de la APP del puente ASIO (C5).
//
// El driver Acoustical Bridge (asio-bridge/) se ve para Cubase como una
// interfaz ASIO virtual. El audio viaja por un bloque de memoria compartida
// de nombre fijo (BridgeShared.h) con dos anillos:
//
//   capture ring   (driver escribe, la app lee): el audio de Cubase — lo que
//        el usuario rutea a las salidas ASIO "Bridge Out L/R". La app lo
//        resamplea a la tasa de la tarjeta real y lo alimenta al motor: pasa
//        por el análisis y por el EQ, y suena por la tarjeta real.
//   playback ring  (la app escribe, driver lee): la señal de prueba
//        (generador) o silencio; el driver la reproduce en las entradas
//        "Bridge In L/R" para que Cubase la oiga/grabe y se verifique el
//        camino.
//
// pullCapture/pushPlayback se llaman desde el callback de audio (hilo de
// audio de la app); connect/disconnect desde el hilo de UI. El único
// intercambio con el driver son los anillos e índices (sin locks entre
// procesos): el anillo es tan largo (~340 ms) que absorbe la deriva de
// relojes, y si la app se atrasa más del anillo se resincroniza a la cola.
#pragma once

#include "BridgeShared.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#ifdef _WIN32
#  include <windows.h>
#endif

class AsioBridgeClient {
public:
    AsioBridgeClient() = default;
    ~AsioBridgeClient() { disconnect(); }

    AsioBridgeClient(const AsioBridgeClient&) = delete;
    AsioBridgeClient& operator=(const AsioBridgeClient&) = delete;

    // Conecta al bloque compartido. Si el driver aún no está activo, lo crea:
    // el driver se une por nombre y respeta el header si la magia ya es
    // válida, así que da igual quién llega primero.
    bool connect() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected_.load(std::memory_order_relaxed)) return true;
        HANDLE hMap = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, bridge::kSharedMemoryName);
        if (!hMap)
            hMap = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        static_cast<DWORD>(bridge::kSharedBlockSize),
                                        bridge::kSharedMemoryName);
        if (!hMap) return false;
        void* view = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0,
                                     bridge::kSharedBlockSize);
        if (!view) {
            ::CloseHandle(hMap);
            return false;
        }
        auto* header = static_cast<bridge::BridgeHeader*>(view);
        if (header->magic != bridge::kMagic) *header = bridge::BridgeHeader{};
        handle_ = hMap;
        header_ = header;
        capPos_ = 0.0;
        pushFrac_ = 0.0;
        connected_.store(true, std::memory_order_relaxed);
        return true;
#else
        return false;
#endif
    }

    void disconnect() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mutex_);
        connected_.store(false, std::memory_order_relaxed);
        if (header_) {
            // UnmapViewOfFile recibe el puntero de la vista (no el handle).
            ::UnmapViewOfFile(header_);
            ::CloseHandle(handle_);
            handle_ = nullptr;
            header_ = nullptr;
        }
#else
        connected_.store(false, std::memory_order_relaxed);
#endif
    }

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

    uint32_t sampleRate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return header_ ? header_->sampleRate : 0;
    }
    uint32_t bufferSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return header_ ? header_->bufferSize : 0;
    }

    // Extrae audio de Cubase (capture ring) resampleado a outSampleRate.
    // Devuelve los fotogramas escritos en outL/outR (mono por array, estéreo).
    // Solo lo toca el hilo de audio.
    int pullCapture(float* outL, float* outR, int maxOutFrames, double outSampleRate) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_ || outSampleRate <= 1.0 || maxOutFrames <= 0) return 0;
        bridge::BridgeHeader& h = *header_;
        if (h.sampleRate == 0) return 0;

        const uint64_t ringFrames = bridge::kRingFrames;
        const float* cap = bridge::captureRing(&h);
        const uint64_t w = h.captureWriteIndex;

        // La app estuvo parada más de lo que cabe en el anillo: se desbordó.
        // Resincroniza a la cola (descarta el audio viejo) y empieza de nuevo.
        if (w - static_cast<uint64_t>(capPos_) > ringFrames) {
            capPos_ = static_cast<double>(w);
            return 0;
        }

        // Interpolación lineal: por cada fotograma de salida consume
        // ringRate/outRate fotogramas del anillo.
        const double step = static_cast<double>(h.sampleRate) / outSampleRate;
        int produced = 0;
        while (produced < maxOutFrames) {
            const uint64_t i0 = static_cast<uint64_t>(capPos_);
            if (i0 + 1 > w) break;  // aún no hay una muestra completa disponible
            const uint64_t a = i0 % ringFrames;
            const uint64_t b = (i0 + 1) % ringFrames;
            const double frac = capPos_ - static_cast<double>(i0);
            outL[produced] = static_cast<float>(
                cap[a * bridge::kChannels] * (1.0 - frac) + cap[b * bridge::kChannels] * frac);
            outR[produced] = static_cast<float>(
                cap[a * bridge::kChannels + 1] * (1.0 - frac) + cap[b * bridge::kChannels + 1] * frac);
            ++produced;
            capPos_ += step;
        }
        // Consume hasta la parte entera (siempre <= a lo realmente usado).
        h.captureReadIndex = static_cast<uint64_t>(capPos_);
        return produced;
    }

    // Envía audio a Cubase (playback ring) resampleado desde inSampleRate a
    // la tasa del anillo. Solo lo toca el hilo de audio.
    void pushPlayback(const float* inL, const float* inR, int inFrames, double inSampleRate) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!header_ || inFrames <= 0 || inSampleRate <= 1.0) return;
        bridge::BridgeHeader& h = *header_;
        if (h.sampleRate == 0) return;

        // Anillo lleno (el driver no consume, p. ej. Cubase parado): descarta
        // en lugar de pisar lo que no ha leído.
        const uint64_t rd = h.playbackReadIndex;
        uint64_t w = h.playbackWriteIndex;
        const uint64_t ringFrames = bridge::kRingFrames;
        if (w - rd >= ringFrames) {
            pushFrac_ = 0.0;
            return;
        }

        float* pb = bridge::playbackRing(&h);
        // Por cada fotograma del anillo produce, consume inRate/ringRate
        // fotogramas de entrada; pushFrac_ es la fracción pendiente del
        // siguiente buffer.
        const double step = inSampleRate / static_cast<double>(h.sampleRate);
        double pos = pushFrac_;
        while (pos + 1.0 <= static_cast<double>(inFrames) && w - rd < ringFrames) {
            const int i0 = static_cast<int>(pos);
            const double frac = pos - static_cast<double>(i0);
            const uint64_t idx = w % ringFrames;
            pb[idx * bridge::kChannels] = static_cast<float>(
                inL[i0] * (1.0 - frac) + inL[i0 + 1] * frac);
            pb[idx * bridge::kChannels + 1] = static_cast<float>(
                inR[i0] * (1.0 - frac) + inR[i0 + 1] * frac);
            ++w;
            pos += step;
        }
        pushFrac_ = pos - static_cast<double>(inFrames);
        h.playbackWriteIndex = w;
    }

private:
    mutable std::mutex mutex_;
    std::atomic<bool> connected_{false};
    bridge::BridgeHeader* header_ = nullptr;
    double capPos_ = 0.0;    // resampler de captura: posición fraccional en el anillo
    double pushFrac_ = 0.0;  // resampler de playback: fracción para el próximo buffer
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#endif
};
