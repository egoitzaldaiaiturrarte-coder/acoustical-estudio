// NoiseProfiler.h — port de NoiseProfiler.kt: perfil de ruido de fondo con
// resta espectral por compuerta.
//
// Thread-safe por sí mismo: el hilo del motor (feedFrame/subtractNoise/
// hasProfile) y la UI (start/cancel/clear) lo usan desde hilos distintos.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

namespace acoustical {

class NoiseProfiler {
public:
    NoiseProfiler(int binCount, int maxCaptureFrames = 50, float gateRange = 6.0f)
        : binCount_(binCount), maxCaptureFrames_(maxCaptureFrames > 0 ? maxCaptureFrames : 1),
          gateRange_(gateRange) {
        accumulated_.assign(binCount, 0.0f);
    }

    // Copia del perfil (segura de mantener; nullptr si no hay perfil).
    std::vector<float> profile() const {
        std::lock_guard<std::mutex> lock(mu_);
        return noiseProfile_ ? *noiseProfile_ : std::vector<float>{};
    }
    bool hasProfile() const {
        std::lock_guard<std::mutex> lock(mu_);
        return noiseProfile_ != nullptr;
    }

    void startCapture() {
        std::lock_guard<std::mutex> lock(mu_);
        std::fill(accumulated_.begin(), accumulated_.end(), 0.0f);
        capturedFrames_ = 0;
        noiseProfile_.reset();
        capturing_.store(true, std::memory_order_release);
    }

    // Devuelve true cuando la captura está completa
    bool feedFrame(const std::vector<float>& magsDb) {
        if (!capturing_.load(std::memory_order_acquire)) return false;
        if (static_cast<int>(magsDb.size()) != binCount_) return false;

        bool done = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (int i = 0; i < binCount_; ++i) accumulated_[i] += magsDb[i];
            if (++capturedFrames_ >= maxCaptureFrames_) {
                finishCaptureLocked();
                done = true;
            }
        }
        return done;
    }

    void cancelCapture() {
        std::lock_guard<std::mutex> lock(mu_);
        capturing_.store(false, std::memory_order_release);
        std::fill(accumulated_.begin(), accumulated_.end(), 0.0f);
        capturedFrames_ = 0;
    }

    bool isCapturing() const { return capturing_.load(std::memory_order_acquire); }

    float captureProgress() const {
        std::lock_guard<std::mutex> lock(mu_);
        return std::clamp(static_cast<float>(capturedFrames_) / maxCaptureFrames_, 0.0f, 1.0f);
    }

    // Resta por compuerta: medido <= ruido → ruido; zona de transición proporcional
    std::vector<float> subtractNoise(const std::vector<float>& magsDb) const {
        std::vector<float> noise;
        {
            std::lock_guard<std::mutex> lock(mu_);
            noise = noiseProfile_ ? *noiseProfile_ : std::vector<float>{};
        }
        if (noise.size() != magsDb.size()) return magsDb;
        std::vector<float> result(magsDb.size());
        for (size_t i = 0; i < magsDb.size(); ++i) {
            const float measured = magsDb[i];
            const float noiseDb = noise[i];
            if (measured <= noiseDb) {
                result[i] = noiseDb;
            } else if (measured < noiseDb + gateRange_) {
                // Zona de transición: resta espectral proporcional en dominio
                // LINEAL. Con dB negativos la fórmula antigua
                // (measured - noiseDb*(1-ratio)) se convertía en SUMA y
                // amplificaba el ruido hasta +40 dB (mismo bug A2 del móvil).
                const float gate = std::clamp((measured - noiseDb) / gateRange_, 0.0f, 1.0f);
                const double linSig = std::pow(10.0, measured / 20.0);
                const double linNo  = std::pow(10.0, noiseDb / 20.0);
                const double linOut = std::max(0.0, linSig - linNo * (1.0 - gate));
                const float out = linOut > 1e-10
                    ? static_cast<float>(20.0 * std::log10(linOut)) : noiseDb;
                result[i] = std::max(noiseDb, out);
            } else {
                result[i] = measured;
            }
        }
        return result;
    }

    void clearProfile() {
        std::lock_guard<std::mutex> lock(mu_);
        noiseProfile_.reset();
    }

private:
    // Requiere mu_ tomado.
    void finishCaptureLocked() {
        auto profile = std::make_unique<std::vector<float>>(binCount_);
        for (int i = 0; i < binCount_; ++i) (*profile)[i] = accumulated_[i] / capturedFrames_;
        noiseProfile_ = std::move(profile);
        capturing_.store(false, std::memory_order_release);
    }

    int binCount_;
    int maxCaptureFrames_;
    float gateRange_;

    mutable std::mutex mu_;
    std::vector<float> accumulated_;
    int capturedFrames_ = 0;
    std::unique_ptr<std::vector<float>> noiseProfile_;
    std::atomic<bool> capturing_{false};
};

} // namespace acoustical
