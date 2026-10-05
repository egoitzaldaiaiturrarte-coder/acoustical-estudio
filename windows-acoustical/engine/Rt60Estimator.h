// Rt60Estimator.h — port de Rt60Estimator.kt: estimación del tiempo de
// reverberación (RT60) por integración hacia atrás de Schroeder sobre la
// energía por banda. Es el indicador de las reflexiones de la sala: la cola
// reverberante que los ecuas dinámicos combaten cortando los picos.
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace acoustical {

class Rt60Estimator {
public:
    Rt60Estimator(int bandCount = 8, int historySize = 64, int sampleIntervalMs = 50)
        : bandCount_(bandCount), historySize_(historySize), sampleIntervalMs_(sampleIntervalMs) {
        energyHistory_.assign(bandCount_, std::vector<float>(historySize_, 0.0f));
        rt60PerBand_.assign(bandCount_, 0.0f);
    }

    // Alimenta un fotograma de niveles por banda (dB) y recalcula el RT60.
    void feedFrame(const std::vector<float>& bandMagnitudesDb) {
        const int bands = std::min<int>(static_cast<int>(bandMagnitudesDb.size()), bandCount_);
        for (int b = 0; b < bands; ++b)
            energyHistory_[b][writeIndex_] = dbToLinear(bandMagnitudesDb[b]);
        writeIndex_ = (writeIndex_ + 1) % historySize_;
        if (framesCollected_ < historySize_) ++framesCollected_;
        if (framesCollected_ >= 8) computeRt60();
    }

    float currentRt60Ms() const { return currentRt60Ms_; }
    const std::vector<float>& rt60PerBand() const { return rt60PerBand_; }

    void reset() {
        for (auto& h : energyHistory_) std::fill(h.begin(), h.end(), 0.0f);
        std::fill(rt60PerBand_.begin(), rt60PerBand_.end(), 0.0f);
        writeIndex_ = 0;
        framesCollected_ = 0;
        currentRt60Ms_ = 0.0f;
    }

private:
    // Schroeder: integra la energía hacia atrás, convierte a dB y ajusta una
    // recta a los primeros 2/3 de la curva de decaimiento.
    void computeRt60() {
        float sumRt60 = 0.0f;
        int validBands = 0;

        for (int b = 0; b < bandCount_; ++b) {
            const auto& history = energyHistory_[b];
            const int n = framesCollected_;
            if (n < 4) continue;

            std::vector<float> edc(n);
            float cumulative = 0.0f;
            for (int i = 0; i < n; ++i) {
                const int idx = (writeIndex_ - 1 - i + historySize_) % historySize_;
                cumulative += history[idx];
                edc[i] = cumulative;
            }

            if (edc[0] <= 0.0f) continue;
            const float maxEdc = edc[0];
            for (int i = 0; i < n; ++i)
                edc[i] = edc[i] > 0.0f ? linearToDb(edc[i] / maxEdc) : -120.0f;

            const int useCount = std::max(4, (n * 2) / 3);
            double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
            for (int i = 0; i < useCount; ++i) {
                const double t = i * static_cast<double>(sampleIntervalMs_);
                const double y = edc[i];
                sumX += t;
                sumY += y;
                sumXY += t * y;
                sumXX += t * t;
            }

            const double denom = static_cast<double>(useCount) * sumXX - sumX * sumX;
            if (denom == 0.0) continue;

            // Pendiente en dB/ms; RT60 = tiempo en caer 60 dB
            const double slope = (static_cast<double>(useCount) * sumXY - sumX * sumY) / denom;
            if (slope >= 0.0) continue;  // no decae: no hay cola medible

            const float rt60 = static_cast<float>(-60.0 / slope);
            if (rt60 > 10.0f && rt60 < 10000.0f) {
                rt60PerBand_[b] = rt60;
                sumRt60 += rt60;
                ++validBands;
            }
        }

        if (validBands > 0) currentRt60Ms_ = sumRt60 / validBands;
    }

    static float dbToLinear(float db) { return std::exp(db / 8.6858896f); }
    static float linearToDb(float linear) {
        return linear > 0.0f ? 8.6858896f * std::log(linear) : -120.0f;
    }

    int bandCount_;
    int historySize_;
    int sampleIntervalMs_;
    std::vector<std::vector<float>> energyHistory_;
    std::vector<float> rt60PerBand_;
    int writeIndex_ = 0;
    int framesCollected_ = 0;
    float currentRt60Ms_ = 0.0f;
};

} // namespace acoustical
