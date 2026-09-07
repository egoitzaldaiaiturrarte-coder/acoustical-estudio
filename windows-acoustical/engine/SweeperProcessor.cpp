#include "SweeperProcessor.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace acoustical {

SweeperProcessor::SweeperProcessor(const std::vector<float>& bandFrequencies, DynamicEqConfig config)
    : bandFrequencies_(bandFrequencies),
      bandCount_(static_cast<int>(bandFrequencies.size())),
      decisionIntervalMs(config.decisionIntervalMs),
      maxGainDb(config.maxGainDb),
      mixerLevel(config.mixerLevel),
      speedMultiplier(config.speedMultiplier),
      extraSweeps(config.extraSweeps),
      direction(config.startFrom),
      channelLinked(true) {
    config.clamp();
    decisionIntervalMs.store(config.decisionIntervalMs, std::memory_order_relaxed);
    maxGainDb.store(config.maxGainDb, std::memory_order_relaxed);
    mixerLevel.store(config.mixerLevel, std::memory_order_relaxed);
    speedMultiplier.store(config.speedMultiplier, std::memory_order_relaxed);
    extraSweeps.store(config.extraSweeps, std::memory_order_relaxed);
    direction.store(config.startFrom, std::memory_order_relaxed);
    autoGainsL_.assign(bandCount_, 0.0f);
    autoTargetsL_.assign(bandCount_, 0.0f);
    autoGainsR_.assign(bandCount_, 0.0f);
    autoTargetsR_.assign(bandCount_, 0.0f);
}

void SweeperProcessor::applyConfig(const DynamicEqConfig& cfg) {
    DynamicEqConfig c = cfg;
    c.clamp();
    decisionIntervalMs.store(c.decisionIntervalMs, std::memory_order_relaxed);
    maxGainDb.store(c.maxGainDb, std::memory_order_relaxed);
    mixerLevel.store(c.mixerLevel, std::memory_order_relaxed);
    speedMultiplier.store(c.speedMultiplier, std::memory_order_relaxed);
    extraSweeps.store(c.extraSweeps, std::memory_order_relaxed);
    direction.store(c.startFrom, std::memory_order_relaxed);
}

// 0 = graves (20 Hz), 1 = agudos (20 kHz), logarítmico
static float freqNorm(float freqHz) {
    return static_cast<float>(std::clamp(std::log10(std::max(freqHz, 20.0f) / 20.0f) / 3.0, 0.0, 1.0));
}

float SweeperProcessor::smoothingMs(float freqHz) const {
    const float speed = speedMultiplier.load(std::memory_order_relaxed);
    return std::max(500.0f * std::pow(10.0f, -2.1f * freqNorm(freqHz)) / speed, 2.0f);
}

std::unique_ptr<SweepStep> SweeperProcessor::step(const std::vector<float>& measuredLevels,
                                                  long long nowMs, float dtMs) {
    if (bandCount_ == 0) return nullptr;

    const float maxGain = maxGainDb.load(std::memory_order_relaxed);
    const float mixer = mixerLevel.load(std::memory_order_relaxed);
    const long long interval = decisionIntervalMs.load(std::memory_order_relaxed);
    const int extra = extraSweeps.load(std::memory_order_relaxed);
    const SweepDirection dir = direction.load(std::memory_order_relaxed);
    const bool linked = channelLinked.load(std::memory_order_relaxed);

    // 1. Ajuste continuo de 10 ms hacia objetivos con suavizado automático por banda
    for (int i = 0; i < bandCount_; ++i) {
        const float factor = std::clamp(dtMs / smoothingMs(bandFrequencies_[i]), 0.0f, 1.0f);
        autoGainsL_[i] += (autoTargetsL_[i] - autoGainsL_[i]) * factor;
        autoGainsR_[i] += (autoTargetsR_[i] - autoGainsR_[i]) * factor;
    }

    // 2. Una decisión cada decisionIntervalMs
    if (nowMs - lastDecisionMs_ < interval) return nullptr;
    lastDecisionMs_ = nowMs;

    // 3. Solo participan bandas con señal real
    std::vector<int> active;
    for (int i = 0; i < bandCount_; ++i)
        if (i < static_cast<int>(measuredLevels.size()) &&
            measuredLevels[i] > NOISE_FLOOR_DB + ACTIVE_MARGIN_DB)
            active.push_back(i);
    if (active.size() < 2) return nullptr;

    float mean = 0.0f;
    for (int i : active) mean += measuredLevels[i];
    mean /= static_cast<float>(active.size());

    std::sort(active.begin(), active.end(), [&](int a, int b) {
        switch (dir) {
            case SweepDirection::NEED_BASED: return measuredLevels[a] > measuredLevels[b];
            case SweepDirection::BOTTOM_UP:  return bandFrequencies_[a] < bandFrequencies_[b];
            case SweepDirection::TOP_DOWN:   return bandFrequencies_[a] > bandFrequencies_[b];
        }
        return measuredLevels[a] > measuredLevels[b];
    });

    const int n = static_cast<int>(active.size());
    int firstCutIdx = -1;
    const char* channel;
    if (linked) {
        channel = "L+R";
        for (int k = 0; k <= extra; ++k) {
            const int rank = ((cursor_ + k) % n + n) % n;
            const int cutIdx = active[rank];
            const int boostIdx = active[(((n - 1 - rank) % n) + n) % n];
            applyDecision(measuredLevels, cutIdx, boostIdx, mean, true, true);
            if (firstCutIdx < 0) firstCutIdx = cutIdx;
        }
    } else {
        const bool toR = nextChannelIsR_;
        nextChannelIsR_ = !nextChannelIsR_;
        channel = toR ? "R" : "L";
        for (int k = 0; k <= extra; ++k) {
            const int rank = ((cursor_ + k) % n + n) % n;
            const int cutIdx = active[rank];
            const int boostIdx = active[(((n - 1 - rank) % n) + n) % n];
            applyDecision(measuredLevels, cutIdx, boostIdx, mean, !toR, toR);
            if (firstCutIdx < 0) firstCutIdx = cutIdx;
        }
    }

    cursor_ += extra + 1;

    auto stepOut = std::make_unique<SweepStep>();
    stepOut->bandIndex = firstCutIdx;
    stepOut->centerFreqHz = bandFrequencies_[firstCutIdx];
    stepOut->gainDb = (std::strcmp(channel, "R") == 0 ? autoGainsR_[firstCutIdx]
                                                      : autoGainsL_[firstCutIdx]) * mixer;
    stepOut->decisionIntervalMs = static_cast<float>(interval);
    stepOut->smoothingMs = smoothingMs(bandFrequencies_[firstCutIdx]);
    stepOut->channel = channel;
    return stepOut;
}

void SweeperProcessor::applyDecision(const std::vector<float>& measuredLevels, int cutIdx,
                                     int boostIdx, float mean, bool toL, bool toR) {
    if (cutIdx == boostIdx) return;
    const float maxGain = maxGainDb.load(std::memory_order_relaxed);
    const float cutDev = std::max(measuredLevels[cutIdx] - mean, 0.0f);
    const float boostDev = std::max(mean - measuredLevels[boostIdx], 0.0f);
    if (toL) {
        if (cutDev > 0.0f)
            autoTargetsL_[cutIdx] = std::clamp(autoTargetsL_[cutIdx] - cutDev, -maxGain, maxGain);
        if (boostDev > 0.0f)
            autoTargetsL_[boostIdx] = std::clamp(autoTargetsL_[boostIdx] + boostDev, -maxGain, maxGain);
    }
    if (toR) {
        if (cutDev > 0.0f)
            autoTargetsR_[cutIdx] = std::clamp(autoTargetsR_[cutIdx] - cutDev, -maxGain, maxGain);
        if (boostDev > 0.0f)
            autoTargetsR_[boostIdx] = std::clamp(autoTargetsR_[boostIdx] + boostDev, -maxGain, maxGain);
    }
}

std::vector<float> SweeperProcessor::gainsL() const {
    const float mixer = mixerLevel.load(std::memory_order_relaxed);
    std::vector<float> out(bandCount_);
    for (int i = 0; i < bandCount_; ++i) out[i] = autoGainsL_[i] * mixer;
    return out;
}

std::vector<float> SweeperProcessor::gainsR() const {
    const float mixer = mixerLevel.load(std::memory_order_relaxed);
    std::vector<float> out(bandCount_);
    for (int i = 0; i < bandCount_; ++i) out[i] = autoGainsR_[i] * mixer;
    return out;
}

void SweeperProcessor::reset() {
    std::fill(autoGainsL_.begin(), autoGainsL_.end(), 0.0f);
    std::fill(autoTargetsL_.begin(), autoTargetsL_.end(), 0.0f);
    std::fill(autoGainsR_.begin(), autoGainsR_.end(), 0.0f);
    std::fill(autoTargetsR_.begin(), autoTargetsR_.end(), 0.0f);
    cursor_ = 0;
    lastDecisionMs_ = 0;
    nextChannelIsR_ = false;
}

} // namespace acoustical
