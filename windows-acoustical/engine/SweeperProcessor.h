// SweeperProcessor.h — port de SweeperProcessor.kt: el corrector automático
// que se repite tres veces (los tres ecuas dinámicos) con distintos ajustes.
#pragma once

#include "AcousticalParameters.h"
#include <atomic>
#include <atomic>
#include <memory>

namespace acoustical {

struct SweepStep {
    int bandIndex = -1;
    float centerFreqHz = 0.0f;
    float gainDb = 0.0f;
    float decisionIntervalMs = 0.0f;
    float smoothingMs = 0.0f;
    const char* channel = "L+R";  // "L+R" enlazado, o el canal corregido ahora
};

class SweeperProcessor {
public:
    SweeperProcessor(const std::vector<float>& bandFrequencies, DynamicEqConfig config);

    // Cambia parámetros en vivo sin perder el progreso
    void applyConfig(const DynamicEqConfig& cfg);

    // Suavizado automático por frecuencia (~4 ms en agudos, relajado en graves)
    float smoothingMs(float freqHz) const;

    // Un tick de 10 ms. Devuelve la decisión aplicada o nullptr si no toca decidir.
    std::unique_ptr<SweepStep> step(const std::vector<float>& measuredLevels, long long nowMs,
                                    float dtMs = 10.0f);

    // Curvas de ganancia post-mezclador por canal
    std::vector<float> gainsL() const;
    std::vector<float> gainsR() const;

    void reset();

    // Parámetros volátiles (ajustables en caliente desde la UI)
    std::atomic<int> decisionIntervalMs;
    std::atomic<float> maxGainDb;
    std::atomic<float> mixerLevel;
    std::atomic<float> speedMultiplier;
    std::atomic<int> extraSweeps;
    std::atomic<SweepDirection> direction;
    std::atomic<bool> channelLinked;   // enlazado: corrige L+R juntos

    static constexpr long long TICK_MS = 10;

private:
    void applyDecision(const std::vector<float>& measuredLevels, int cutIdx, int boostIdx,
                       float mean, bool toL, bool toR);

    static constexpr float NOISE_FLOOR_DB = -120.0f;
    static constexpr float ACTIVE_MARGIN_DB = 3.0f;

    std::vector<float> bandFrequencies_;
    int bandCount_;
    std::vector<float> autoGainsL_, autoTargetsL_, autoGainsR_, autoTargetsR_;
    int cursor_ = 0;
    long long lastDecisionMs_ = 0;
    bool nextChannelIsR_ = false;
};

} // namespace acoustical
