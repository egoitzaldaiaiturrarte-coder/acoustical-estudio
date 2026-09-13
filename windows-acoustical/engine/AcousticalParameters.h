// AcousticalParameters.h — inventario completo de parámetros, port exacto de la
// app Android (AudioModels.kt / ControllerModels.kt / SweeperProcessor.kt).
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace acoustical {

// === Enums con los mismos valores que la app móvil ===

enum class SampleRate { Hz44100, Hz48000, Hz88200, Hz96000 };

inline int sampleRateHz(SampleRate sr) {
    switch (sr) {
        case SampleRate::Hz44100: return 44100;
        case SampleRate::Hz48000: return 48000;
        case SampleRate::Hz88200: return 88200;
        case SampleRate::Hz96000: return 96000;
    }
    return 48000;
}

enum class FftSize { S512, S1024, S2048, S4096, S8192 };

inline int fftSamples(FftSize s) {
    switch (s) {
        case FftSize::S512: return 512;
        case FftSize::S1024: return 1024;
        case FftSize::S2048: return 2048;
        case FftSize::S4096: return 4096;
        case FftSize::S8192: return 8192;
    }
    return 2048;
}

enum class AnalysisInterval { Fast25, Normal50, Balanced100, Eco200, PowerSaver500 };

inline long analysisIntervalMs(AnalysisInterval i) {
    switch (i) {
        case AnalysisInterval::Fast25: return 25;
        case AnalysisInterval::Normal50: return 50;
        case AnalysisInterval::Balanced100: return 100;
        case AnalysisInterval::Eco200: return 200;
        case AnalysisInterval::PowerSaver500: return 500;
    }
    return 50;
}

enum class BandCount { B8, B10, B16, B31, B124 };

enum class BitDepth { Int16, Int24 };

enum class ProbeQuality { Low, Normal, High };

enum class SweepDirection {
    NEED_BASED,   // Va siempre donde más se necesita
    BOTTOM_UP,    // Recorre el espectro de los graves hacia arriba
    TOP_DOWN      // Recorre el espectro de los agudos hacia abajo
};

enum class SweepProcess { EQ_1, EQ_2, EQ_3 };

inline constexpr int kDynamicEqCount = 3;

// === Banda de apoyo de frecuencia libre ===

struct SupportBand {
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 2.0f;
};

// === Ajustes de un ecu dinámico (los tres son el mismo corrector) ===

struct DynamicEqConfig {
    SweepDirection startFrom = SweepDirection::NEED_BASED;
    int decisionIntervalMs = 800;
    float maxGainDb = 12.0f;
    float mixerLevel = 0.8f;
    float speedMultiplier = 1.0f;  // ×0.5 … ×4 sobre el suavizado automático
    int extraSweeps = 1;           // pares de bandas extra por decisión (0–6)

    static constexpr int MIN_INTERVAL_MS = 100;
    static constexpr int MAX_INTERVAL_MS = 2000;
    static constexpr float MIN_GAIN_DB = 1.0f;
    static constexpr float MAX_GAIN_DB = 50.0f;
    static constexpr float MIN_SPEED = 0.5f;
    static constexpr float MAX_SPEED = 4.0f;
    static constexpr int MAX_EXTRA_SWEEPS = 6;

    void clamp() {
        decisionIntervalMs = std::clamp(decisionIntervalMs, MIN_INTERVAL_MS, MAX_INTERVAL_MS);
        maxGainDb = std::clamp(maxGainDb, MIN_GAIN_DB, MAX_GAIN_DB);
        mixerLevel = std::clamp(mixerLevel, 0.0f, 1.0f);
        speedMultiplier = std::clamp(speedMultiplier, MIN_SPEED, MAX_SPEED);
        extraSweeps = std::clamp(extraSweeps, 0, MAX_EXTRA_SWEEPS);
    }
};

// === Configuración central del motor ===

struct AudioConfig {
    SampleRate sampleRate = SampleRate::Hz96000;
    FftSize fftSize = FftSize::S2048;
    AnalysisInterval analysisInterval = AnalysisInterval::Normal50;
    BandCount bandCount = BandCount::B10;
    bool correctionEnabled = true;
    float maxGainDb = 12.0f;
    float targetSpl = 75.0f;
    float smoothingFactor = 0.3f;
    float noiseFloorDb = -120.0f;
    bool noiseSubtractionEnabled = true;
    float audioDelayMs = 25.0f;
    bool geoAutoAdjust = false;
    std::string scenarioPreset = "CUSTOM";
    // Cadencia de la corrección rápida (ms). A menor valor, más rápido se
    // recorre el espectro de bandas; 500 ms es el valor original.
    int correctionIntervalMs = 500;

    // Con límites altos de corrección el suavizado se relaja para evitar oscilación
    float effectiveSmoothingFactor() const {
        return maxGainDb > 24.0f ? std::max(smoothingFactor * 24.0f / maxGainDb, 0.05f)
                                 : smoothingFactor;
    }
};

struct AutoCheckConfig {
    bool enabled = false;
    int intervalSeconds = 60;               // 15 / 30 / 60 / 120 s
    ProbeQuality quality = ProbeQuality::Normal;
    BitDepth bitDepth = BitDepth::Int24;
};

enum class WorkEnvironmentType { Room, SmallHall, Hall, Outdoor };

inline float environmentWidth(WorkEnvironmentType t) {
    switch (t) {
        case WorkEnvironmentType::Room: return 8.0f;
        case WorkEnvironmentType::SmallHall: return 15.0f;
        case WorkEnvironmentType::Hall: return 30.0f;
        case WorkEnvironmentType::Outdoor: return 100.0f;
    }
    return 8.0f;
}

inline float environmentDepth(WorkEnvironmentType t) {
    switch (t) {
        case WorkEnvironmentType::Room: return 6.0f;
        case WorkEnvironmentType::SmallHall: return 10.0f;
        case WorkEnvironmentType::Hall: return 20.0f;
        case WorkEnvironmentType::Outdoor: return 100.0f;
    }
    return 6.0f;
}

enum class ReferenceSource { PresetSequence, SineSweep, PinkNoise, External };
enum class StereoMode { Linked, Unlinked };
enum class AppMode { Controller, Analyzer };

// === Posición espacial y compensación SPL (port de ControllerModels.kt) ===

struct SpatialPosition {
    float x = 0.0f;      // -1 … 1 (izq/der)
    float y = 0.0f;      // -1 … 1 (atrás/frente)
    float z = 0.5f;      // 0 … 1 (0 = cerca, 1 = lejos)
    float size = 0.5f;   // 0 … 1 (0 = pequeño, 1 = grande)
};

struct SplCompensation {
    float gainAdjustDb = 0.0f;
    float delayAdjustMs = 0.0f;
    bool isActive = true;

    // Ley inversa del cuadrado: +6 dB por duplicar distancia; velocidad del
    // sonido ~343 m/s para el retardo.
    static SplCompensation calculate(const SpatialPosition& pos, float /*targetSpl*/,
                                     WorkEnvironmentType /*env*/) {
        const float distanceMeters = pos.z * 15.0f;
        const float distanceGain = distanceMeters > 0.5f
            ? 20.0f * std::log10(distanceMeters / 1.0f) : 0.0f;
        const float sizeGain = (0.5f - pos.size) * 6.0f;
        const float lateralGain = std::abs(pos.x) * 1.5f;
        return { distanceGain + sizeGain + lateralGain, (distanceMeters / 343.0f) * 1000.0f, true };
    }
};

struct WorkConfig {
    std::string name = "Trabajo 1";
    WorkEnvironmentType environment = WorkEnvironmentType::Room;
    float roomWidthM = 8.0f;
    float roomDepthM = 6.0f;
    ReferenceSource referenceSource = ReferenceSource::PresetSequence;
    StereoMode stereoMode = StereoMode::Linked;
    AppMode appMode = AppMode::Controller;
    AutoCheckConfig autoCheck;
    BitDepth bitDepth = BitDepth::Int24;
    SampleRate workSampleRate = SampleRate::Hz48000;
};

// === Banda del EQ y fotograma de espectro ===

struct EqBand {
    int index = 0;
    float centerFreq = 1000.0f;
    float gainDb = 0.0f;
    float targetGainDb = 0.0f;
    float q = 1.41f;

    bool isClampedAt(float maxGainDb) const {
        return maxGainDb > 0.0f && std::abs(gainDb) >= maxGainDb - 0.5f;
    }
};

struct SpectrumFrame {
    std::vector<float> magnitudesDb;
    std::vector<float> frequencies;
    long long timestampMs = 0;
    int size() const { return static_cast<int>(magnitudesDb.size()); }
};

} // namespace acoustical
