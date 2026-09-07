#include "AcousticalEngine.h"
#include <algorithm>
#include <iterator>
#include <memory>

namespace acoustical {

AcousticalEngine::AcousticalEngine() {
    configure(AudioConfig{});
    dynamicCfgs_ = {
        DynamicEqConfig{SweepDirection::NEED_BASED, 800, 12.0f, 0.8f, 1.0f, 1},
        DynamicEqConfig{SweepDirection::BOTTOM_UP, 800, 12.0f, 0.8f, 1.0f, 1},
        DynamicEqConfig{SweepDirection::TOP_DOWN, 800, 12.0f, 0.8f, 1.0f, 1}
    };
    sweeperEnabled_.assign(kDynamicEqCount, false);
    supportBandsByEq_.assign(kDynamicEqCount, {});
    for (int i = 0; i < kDynamicEqCount; ++i)
        sweepers_.push_back(std::make_unique<SweeperProcessor>(bandFrequencies_, dynamicCfgs_[i]));
}

void AcousticalEngine::configure(const AudioConfig& config) {
    config_ = config;
    bandFrequencies_ = StandardFrequencies::forCount(config.bandCount);
    fft_ = std::make_unique<FftProcessor>(fftSamples(config.fftSize));
    corrector_ = std::make_unique<RoomCorrector>(
        bandFrequencies_, sampleRateHz(config.sampleRate), fft_->binCount(),
        config.maxGainDb, config.effectiveSmoothingFactor(), config.noiseFloorDb);
    splMeter_ = std::make_unique<SplMeter>(120.0f);
    noiseProfiler_ = std::make_unique<NoiseProfiler>(fft_->binCount(), 50, 6.0f);

    bands_.clear();
    for (int i = 0; i < static_cast<int>(bandFrequencies_.size()); ++i)
        bands_.push_back(EqBand{i, bandFrequencies_[i], 0.0f, 0.0f, 1.41f});

    const float sr = static_cast<float>(sampleRateHz(config.sampleRate));
    dspL_.prepare(static_cast<int>(bandFrequencies_.size()), bandFrequencies_.data(), sr);
    dspR_.prepare(static_cast<int>(bandFrequencies_.size()), bandFrequencies_.data(), sr);

    // Los tres ecuas dinámicos se recrean con las nuevas bandas conservando
    // sus ajustes (igual que AudioEngine.configure en el móvil)
    if (dynamicCfgs_.size() < static_cast<size_t>(kDynamicEqCount)) {
        dynamicCfgs_ = {
            DynamicEqConfig{SweepDirection::NEED_BASED, 800, 12.0f, 0.8f, 1.0f, 1},
            DynamicEqConfig{SweepDirection::BOTTOM_UP, 800, 12.0f, 0.8f, 1.0f, 1},
            DynamicEqConfig{SweepDirection::TOP_DOWN, 800, 12.0f, 0.8f, 1.0f, 1}
        };
    }
    sweepers_.clear();
    for (int i = 0; i < kDynamicEqCount; ++i)
        sweepers_.push_back(std::make_unique<SweeperProcessor>(bandFrequencies_, dynamicCfgs_[i]));
    if (sweeperEnabled_.size() < static_cast<size_t>(kDynamicEqCount))
        sweeperEnabled_.assign(kDynamicEqCount, false);
}

std::vector<EqBand> AcousticalEngine::bands() const {
    std::lock_guard<std::mutex> lock(ringMutex_);
    return bands_;
}

void AcousticalEngine::setBandGain(int index, float gainDb) {
    if (index < 0 || index >= static_cast<int>(bands_.size())) return;
    const float clamped = std::clamp(gainDb, -config_.maxGainDb, config_.maxGainDb);
    bands_[index].gainDb = clamped;
    bands_[index].targetGainDb = clamped;
}

void AcousticalEngine::resetBands() {
    for (auto& b : bands_) { b.gainDb = 0.0f; b.targetGainDb = 0.0f; }
    if (corrector_) corrector_->reset();
}

void AcousticalEngine::pushSamples(const float* samples, int count, int sampleRate) {
    inputSampleRate_.store(sampleRate, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(ringMutex_);
    for (int i = 0; i < count; ++i) {
        ring_.push_back(samples[i]);
        if (ring_.size() > ringCapacity_) ring_.pop_front();
    }
}

void AcousticalEngine::setSecondaryCaptureLevels(const std::vector<float>& levels) {
    std::lock_guard<std::mutex> lock(secondaryMutex_);
    secondaryLevels_ = levels;
}

bool AcousticalEngine::start() {
    if (running_.load()) return true;
    running_.store(true);
    framesAnalyzed_.store(0);
    lastAnalysisMs_.store(0);
    engineThread_ = std::thread(&AcousticalEngine::engineLoop, this);
    return true;
}

void AcousticalEngine::stop() {
    running_.store(false);
    if (engineThread_.joinable()) engineThread_.join();
}

void AcousticalEngine::engineLoop() {
    std::vector<float> block(fftSamples(config_.fftSize));
    const int fftN = fftSamples(config_.fftSize);

    while (running_.load()) {
        const auto tickStart = std::chrono::steady_clock::now();
        const long long now = nowMs();

        // 1. Tick de los tres ecuas dinámicos (cada 10 ms, valores se ajustan aquí)
        std::vector<float> measured;
        {
            std::lock_guard<std::mutex> lock(secondaryMutex_);
            measured = lastMeasuredLevels_;
        }
        if (!measured.empty()) {
            for (int i = 0; i < kDynamicEqCount; ++i) {
                if (!sweeperEnabled_[i]) continue;
                if (auto step = sweepers_[i]->step(measured, now))
                    if (onSweep) onSweep(static_cast<SweepProcess>(i), *step);
            }
        }

        // 2. Análisis FFT cada analysisInterval ms
        bool doAnalysis = (now - lastAnalysisMs_.load()) >= analysisIntervalMs(config_.analysisInterval);
        if (doAnalysis) {
            bool haveBlock = false;
            {
                std::lock_guard<std::mutex> lock(ringMutex_);
                if (static_cast<int>(ring_.size()) >= fftN) {
                    const int start = static_cast<int>(ring_.size()) - fftN;
                    std::copy_n(std::next(ring_.begin(), start), fftN, block.begin());
                    haveBlock = true;
                }
            }
            if (haveBlock) analyzeFrame(block, inputSampleRate_.load());
            lastAnalysisMs_.store(now);
        }

        // Ritmo de tick de 10 ms
        const auto elapsed = std::chrono::steady_clock::now() - tickStart;
        const auto period = std::chrono::milliseconds(SweeperProcessor::TICK_MS);
        if (elapsed < period) std::this_thread::sleep_for(period - elapsed);
    }
}

void AcousticalEngine::analyzeFrame(const std::vector<float>& samples, int sampleRate) {
    if (!fft_ || !corrector_ || !splMeter_) return;

    const float spl = splMeter_->computeSpl(samples.data(), static_cast<int>(samples.size()));
    const auto& rawMags = fft_->computeMagnitudesDb(samples.data(), sampleRate);
    const auto& binFreqs = fft_->binFrequencies(sampleRate);

    // Sustracción de ruido si está activada y hay perfil
    std::vector<float> magsDb;
    if (config_.noiseSubtractionEnabled && noiseProfiler_->hasProfile())
        magsDb = noiseProfiler_->subtractNoise(rawMags);
    else
        magsDb.assign(rawMags.begin(), rawMags.end());

    // Alimenta el capturador de ruido si está grabando
    if (noiseProfiler_->isCapturing()) {
        std::vector<float> raw(rawMags.begin(), rawMags.end());
        const bool done = noiseProfiler_->feedFrame(raw);
        if (onNoiseCaptureProgress) onNoiseCaptureProgress(noiseProfiler_->captureProgress());
        if (done) {
            if (onNoiseCaptureComplete) onNoiseCaptureComplete();
            corrector_->reset();
            for (auto& s : sweepers_) if (s) s->reset();
        }
    }

    SpectrumFrame spectrum;
    spectrum.magnitudesDb = magsDb;
    spectrum.frequencies.assign(binFreqs.begin(), binFreqs.end());
    spectrum.timestampMs = nowMs();

    // Agregación en bandas perceptuales
    std::vector<float> measured = corrector_->aggregateBands(magsDb, binFreqs);
    {
        std::lock_guard<std::mutex> lock(secondaryMutex_);
        lastMeasuredLevels_ = measured;
        // Mezcla de entradas: cada banda usa el nivel más fuerte
        if (secondaryLevels_.size() == measured.size())
            for (size_t i = 0; i < measured.size(); ++i)
                measured[i] = std::max(measured[i], secondaryLevels_[i]);
    }

    // Correcciones del EQ principal
    std::vector<EqBand> correctedBands = bands_;
    float correctionIntensity = 0.0f;
    if (config_.correctionEnabled) {
        const bool hasRef = referenceCaptured_.load();
        const std::vector<float> flatRef(bandFrequencies_.size(), 0.0f);
        const std::vector<float>& ref = hasRef ? referenceLevels_ : flatRef;
        correctedBands = corrector_->computeCorrections(ref, measured, bands_);
        bands_ = correctedBands;
        correctionIntensity = RoomCorrector::correctionIntensity(correctedBands, config_.maxGainDb);
    }

    // Curvas combinadas por canal: EQ manual + ecuas dinámicos + bandas de apoyo
    std::vector<std::vector<float>> gainsL(kDynamicEqCount), gainsR(kDynamicEqCount);
    std::vector<float> manual(bandFrequencies_.size(), 0.0f);
    for (size_t i = 0; i < bands_.size(); ++i) manual[i] = bands_[i].gainDb;

    std::vector<float> combinedL = manual, combinedR = manual;
    for (int i = 0; i < kDynamicEqCount; ++i) {
        gainsL[i] = addSupportGains(sweepers_[i]->gainsL(), i);
        gainsR[i] = addSupportGains(sweepers_[i]->gainsR(), i);
        for (size_t b = 0; b < combinedL.size(); ++b) {
            combinedL[b] = std::clamp(combinedL[b] + gainsL[i][b],
                                      -DynamicEqConfig::MAX_GAIN_DB, DynamicEqConfig::MAX_GAIN_DB);
            combinedR[b] = std::clamp(combinedR[b] + gainsR[i][b],
                                      -DynamicEqConfig::MAX_GAIN_DB, DynamicEqConfig::MAX_GAIN_DB);
        }
    }

    dspL_.setGains(false, combinedL);
    dspR_.setGains(true, combinedR);

    // Osciloscopio: bloque temporal reciente
    std::vector<float> timeSamples(samples.begin(),
                                   samples.begin() + std::min<size_t>(samples.size(), 1024));

    AnalysisResult result;
    result.spectrum = std::move(spectrum);
    result.bands = correctedBands;
    result.measuredBandLevels = measured;
    result.spl = spl;
    result.peakSpl = splMeter_->peakSpl();
    result.averageSpl = splMeter_->averageSpl();
    result.correctionIntensity = correctionIntensity;
    result.framesAnalyzed = framesAnalyzed_.load();
    result.noiseProfile = noiseProfiler_->profile();
    result.combinedGainsL = std::move(combinedL);
    result.combinedGainsR = std::move(combinedR);
    result.dynamicEqGainsL = std::move(gainsL);
    result.dynamicEqGainsR = std::move(gainsR);
    result.timeSamples = std::move(timeSamples);

    framesAnalyzed_.fetch_add(1);
    if (onAnalysis) onAnalysis(result);
}

// === Bandas de apoyo de frecuencia libre (una serie por ecu dinámico) ===

float AcousticalEngine::supportTaper(float freqHz, const SupportBand& band) const {
    if (band.gainDb == 0.0f || band.frequencyHz <= 0.0f) return 0.0f;
    const float octaves = std::abs(std::log10(freqHz / band.frequencyHz));
    const float width = 1.0f / std::max(band.q, 0.5f);
    return octaves < width ? band.gainDb * (1.0f - octaves / width) : 0.0f;
}

float AcousticalEngine::supportGainAt(float freqHz) const {
    float gain = 0.0f;
    for (const auto& eqBands : supportBandsByEq_)
        for (const auto& band : eqBands) gain += supportTaper(freqHz, band);
    return gain;
}

std::vector<float> AcousticalEngine::addSupportGains(const std::vector<float>& gains, int eqIndex) const {
    const auto& support = supportBandsByEq_[eqIndex];
    if (support.empty()) return gains;
    std::vector<float> out = gains;
    for (int b = 0; b < static_cast<int>(out.size()); ++b) {
        float extra = 0.0f;
        for (const auto& band : support) extra += supportTaper(bandFrequencies_[b], band);
        out[b] = std::clamp(out[b] + extra, -DynamicEqConfig::MAX_GAIN_DB, DynamicEqConfig::MAX_GAIN_DB);
    }
    return out;
}

SpectrumFrame AcousticalEngine::applySupportBands(const SpectrumFrame& spectrum) const {
    if (std::all_of(supportBandsByEq_.begin(), supportBandsByEq_.end(),
                    [](const auto& v) { return v.empty(); })) return spectrum;
    SpectrumFrame corrected = spectrum;
    for (size_t i = 0; i < corrected.magnitudesDb.size(); ++i)
        corrected.magnitudesDb[i] += supportGainAt(corrected.frequencies[i]);
    return corrected;
}

// === Referencia y ruido ===

void AcousticalEngine::captureReference() {
    referenceLevels_ = corrector_->currentBandLevels();
    referenceCaptured_.store(true);
    corrector_->reset();
    for (auto& s : sweepers_) if (s) s->reset();
}

void AcousticalEngine::clearReference() {
    referenceCaptured_.store(false);
    referenceLevels_.clear();
}

void AcousticalEngine::startNoiseCapture() { noiseProfiler_->startCapture(); }
void AcousticalEngine::cancelNoiseCapture() { noiseProfiler_->cancelCapture(); }
void AcousticalEngine::clearNoiseProfile() { noiseProfiler_->clearProfile(); }
bool AcousticalEngine::hasNoiseProfile() const { return noiseProfiler_->hasProfile(); }

// === Ecuas dinámicos ===

SweeperProcessor& AcousticalEngine::sweeperFor(int index) {
    return *sweepers_[std::clamp(index, 0, kDynamicEqCount - 1)];
}

void AcousticalEngine::setDynamicEqInterval(int index, int ms) {
    if (index < 0 || index >= kDynamicEqCount) return;
    DynamicEqConfig cfg = dynamicCfgs_[index];
    cfg.decisionIntervalMs = ms;
    setDynamicEqConfig(index, cfg);
}

void AcousticalEngine::setDynamicEqMaxGain(int index, float gainDb) {
    if (index < 0 || index >= kDynamicEqCount) return;
    DynamicEqConfig cfg = dynamicCfgs_[index];
    cfg.maxGainDb = gainDb;
    setDynamicEqConfig(index, cfg);
}

void AcousticalEngine::setDynamicEqSpeed(int index, float speed) {
    if (index < 0 || index >= kDynamicEqCount) return;
    DynamicEqConfig cfg = dynamicCfgs_[index];
    cfg.speedMultiplier = speed;
    setDynamicEqConfig(index, cfg);
}

void AcousticalEngine::setDynamicEqExtras(int index, int extras) {
    if (index < 0 || index >= kDynamicEqCount) return;
    DynamicEqConfig cfg = dynamicCfgs_[index];
    cfg.extraSweeps = extras;
    setDynamicEqConfig(index, cfg);
}

void AcousticalEngine::setDynamicEqConfig(int index, const DynamicEqConfig& cfg) {
    if (index < 0 || index >= kDynamicEqCount) return;
    dynamicCfgs_[index] = cfg;
    if (sweepers_[index]) sweepers_[index]->applyConfig(cfg);
}

void AcousticalEngine::setDynamicEqEnabled(int index, bool enabled) {
    if (index < 0 || index >= kDynamicEqCount) return;
    sweeperEnabled_[index] = enabled;
}

void AcousticalEngine::setDynamicEqMixerLevel(int index, float level) {
    if (index < 0 || index >= kDynamicEqCount || !sweepers_[index]) return;
    sweepers_[index]->mixerLevel.store(std::clamp(level, 0.0f, 1.0f), std::memory_order_relaxed);
}

void AcousticalEngine::setEqChannelLinked(bool linked) {
    for (auto& s : sweepers_) if (s) s->channelLinked.store(linked, std::memory_order_relaxed);
}

void AcousticalEngine::setSupportBands(int index, const std::vector<SupportBand>& bands) {
    if (index < 0 || index >= kDynamicEqCount) return;
    supportBandsByEq_[index] = bands;
}

} // namespace acoustical
