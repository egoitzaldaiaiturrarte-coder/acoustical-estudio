#include "EqDsp.h"
#include "acoustical_dsp.h"
#include <algorithm>
#include <cmath>

namespace acoustical {

// Los coeficientes del peaking EQ (RBJ cookbook) viven en el núcleo DSP
// compartido (dsp/acoustical_dsp.c), que es la única fuente de verdad y la
// que cubren los tests.
BiquadCoeffs makePeaking(float f0, float fs, float gainDb, float q) {
    acoustical_biquad_coeffs c;
    acoustical_make_peaking(f0, fs, gainDb, q, &c);
    BiquadCoeffs out;
    out.b0 = c.b0; out.b1 = c.b1; out.b2 = c.b2; out.a1 = c.a1; out.a2 = c.a2;
    return out;
}

EqDsp::EqDsp() {
    // Snapshot plano inicial (0 bandas) para que process() nunca vea nullptr.
    std::atomic_store(&current_, std::make_shared<EqCoeffSet>());
}

std::shared_ptr<EqCoeffSet> EqDsp::buildSet(const std::vector<float>& gainsDb) const {
    auto s = std::make_shared<EqCoeffSet>();
    s->bands = bands_;
    for (int i = 0; i < bands_; ++i) {
        const float g = i < static_cast<int>(gainsDb.size()) ? gainsDb[i] : 0.0f;
        s->gain[i] = g;
        s->coeffs[i] = makePeaking(bandFreqs_[i], sampleRate_, g, q_);
    }
    return s;
}

void EqDsp::prepare(const std::vector<float>& bandFrequencies, float sampleRate, float q) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    // Validación: con parámetros corruptos no se publica ningún snapshot — se
    // conserva el banco anterior. sampleRate <= 0 / NaN daría coeficientes
    // Inf/NaN, y un vector con menos frecuencias de las usadas leía fuera de
    // rango (assign sobre data sin chequear).
    if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate) || bandFrequencies.empty()) return;
    for (float f : bandFrequencies)
        if (!(f > 0.0f) || !std::isfinite(f)) return;

    bands_ = std::min(static_cast<int>(bandFrequencies.size()), kMaxBands);
    sampleRate_ = sampleRate;
    q_ = q;
    bandFreqs_.assign(bandFrequencies.begin(), bandFrequencies.begin() + bands_);
    std::vector<float> flat(bands_, 0.0f);
    std::atomic_store(&current_, buildSet(flat));
    // El estado de los biquads se pone a cero en el hilo de audio (seguro).
    stateReset_.store(true, std::memory_order_release);
}

void EqDsp::setGains(const std::vector<float>& combinedGainsDb) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    std::atomic_store(&current_, buildSet(combinedGainsDb));
}

void EqDsp::reprepare(float sampleRate) {
    std::lock_guard<std::mutex> lock(writerMutex_);
    // Mismas validaciones que prepare(): tasa corrupta o banco sin preparar →
    // se conserva el snapshot actual (no se publica nada).
    if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate) || bands_ == 0) return;
    // Se conservan las ganancias del último snapshot publicado: así un cambio
    // de tasa de muestreo no deja una ventana plana (sin corrección) hasta el
    // próximo setGains del motor.
    auto prev = std::atomic_load(&current_);
    std::vector<float> prevGains;
    if (prev) prevGains.assign(prev->gain.begin(), prev->gain.begin() + bands_);
    sampleRate_ = sampleRate;
    std::atomic_store(&current_, buildSet(prevGains));
    // El estado de los biquads se pone a cero en el hilo de audio (seguro).
    stateReset_.store(true, std::memory_order_release);
}

void EqDsp::process(float* samples, int numSamples) {
    auto snap = std::atomic_load(&current_);
    if (!snap) return;

    if (stateReset_.exchange(false, std::memory_order_relaxed)) {
        z1_.fill(0.0f);
        z2_.fill(0.0f);
        lastGain_.fill(0.0f);
    }

    const int n = std::min(snap->bands, kMaxBands);
    for (int i = 0; i < n; ++i) {
        const float g = snap->gain[i];
        const bool active = std::abs(g) >= 0.001f;
        const bool wasActive = std::abs(lastGain_[i]) >= 0.001f;
        // (Re)activación de la banda: reinicia el estado para evitar un clic.
        if (active != wasActive) { z1_[i] = 0.0f; z2_[i] = 0.0f; }
        lastGain_[i] = g;
        if (!active) continue;  // banda sin ganancia: sin filtrar

        const BiquadCoeffs& c = snap->coeffs[i];
        float& z1 = z1_[i];
        float& z2 = z2_[i];
        // El cálculo del biquad vive en el núcleo compartido (acoustical_biquad_process):
        // antes la misma matemática estaba duplicada inline aquí, y una corrección
        // futura en el core no llegaría al audio real. state[2] = (z1, z2), mismo
        // estado que antes (se guarda al terminar el bloque).
        acoustical_biquad_coeffs cc;
        cc.b0 = c.b0; cc.b1 = c.b1; cc.b2 = c.b2; cc.a1 = c.a1; cc.a2 = c.a2;
        float state[2] = {z1, z2};
        for (int s = 0; s < numSamples; ++s)
            samples[s] = acoustical_biquad_process(&cc, state, samples[s]);
        z1 = state[0];
        z2 = state[1];
    }
}

} // namespace acoustical
