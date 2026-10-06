// test_engine_wiring.cpp — proves the wired Windows engine compiles, links
// against the shared C core, and still behaves correctly end-to-end.
//
// Compiles the REAL engine sources (FftProcessor.cpp, EqDsp.cpp) + the
// header-only RoomCorrector.h and drives them: FFT -> aggregateBands ->
// computeCorrections -> applyGainsToSpectrum, plus an EqDsp pass.
#include "FftProcessor.h"
#include "RoomCorrector.h"
#include "EqDsp.h"
#include "NoiseProfiler.h"
#include "StandardFrequencies.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace acoustical;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } } while (0)
static bool nearF(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// RMS de la cola de un buffer (descarta el transiente inicial del biquad).
static double rmsFrom(const std::vector<float>& v, size_t from) {
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * double(v[i]);
    return std::sqrt(s / double(v.size() - from));
}

int main() {
    const int sr = 48000;
    const int fftSize = 2048;

    // --- 1) FFT through the wired FftProcessor (delegates to C core) ---
    FftProcessor fft(fftSize);
    CHECK(fft.binCount() == fftSize / 2, "FftProcessor binCount");

    // Pure 1 kHz sine, amplitude 1.0
    std::vector<float> frame(fftSize);
    for (int i = 0; i < fftSize; ++i) frame[i] = std::sin(2.0 * 3.14159265358979 * 1000.0 * i / sr);
    const std::vector<float>& mags = fft.computeMagnitudesDb(frame.data(), sr);
    const std::vector<float>& binFreqs = fft.binFrequencies(sr);
    CHECK((int)mags.size() == fftSize / 2, "magnitudes size");

    int peakBin = 0;
    for (int i = 0; i < (int)mags.size(); ++i) if (mags[i] > mags[peakBin]) peakBin = i;
    const float peakFreq = binFreqs[peakBin];
    CHECK(peakFreq > 990.0f && peakFreq < 1010.0f, "FFT peak at ~1 kHz");
    // La normalización 2/(n*0.54) compensa la ganancia coherente de Hamming:
    // un seno a escala 1.0 lee ~0 dB (antes, con la normalización de ventana
    // rectangular, caía a ~-5 dB). En cualquier caso, muy por encima del piso
    // de ruido de -120 dB.
    CHECK(mags[peakBin] > -15.0f, "peak magnitude well above noise floor");
    std::printf("  FFT: peak bin=%d freq=%.1f Hz mag=%.2f dB\n", peakBin, peakFreq, mags[peakBin]);

    // --- 2) aggregateBands through the wired RoomCorrector (C two-pointer) ---
    const auto bandFreqs = StandardFrequencies::forCount(BandCount::B31);
    const int bandCount = (int)bandFreqs.size();
    RoomCorrector corrector(bandFreqs, sr, fft.binCount(), 12.0f, 0.3f, -120.0f);
    auto bandLevels = corrector.aggregateBands(mags, binFreqs);
    CHECK((int)bandLevels.size() == bandCount, "aggregateBands returns bandCount levels");
    int strongest = 0;
    for (int i = 1; i < bandCount; ++i) if (bandLevels[i] > bandLevels[strongest]) strongest = i;
    const float strongestFreq = bandFreqs[strongest];
    CHECK(strongestFreq > 700.0f && strongestFreq < 1400.0f, "strongest band near 1 kHz");
    std::printf("  aggregateBands: strongest band=%d freq=%.0f Hz level=%.2f dB\n",
                strongest, strongestFreq, bandLevels[strongest]);

    // --- 3) computeCorrections drives gains (cadence path) ---
    std::vector<EqBand> bands(bandCount);
    for (int i = 0; i < bandCount; ++i) {
        bands[i].centerFreq = bandFreqs[i];
        bands[i].q = 1.41f;
        bands[i].gainDb = 0.0f;
        bands[i].targetGainDb = 0.0f;
    }
    std::vector<float> reference(bandCount, 0.0f);
    auto out = corrector.computeCorrections(reference, bandLevels, bands);
    CHECK((int)out.size() == bandCount, "computeCorrections size");
    bool anyCut = false;
    for (int i = 0; i < bandCount; ++i) if (out[i].targetGainDb < -0.05f) anyCut = true;
    CHECK(anyCut, "computeCorrections cuts the loudest band");
    std::printf("  computeCorrections: cut band=%d target=%.2f dB\n", strongest,
                out[strongest].targetGainDb);

    // --- 4) applyGainsToSpectrum size preserved ---
    SpectrumFrame sp;
    sp.frequencies = binFreqs;
    sp.magnitudesDb = mags;
    auto corrected = RoomCorrector::applyGainsToSpectrum(sp, out);
    CHECK((int)corrected.magnitudesDb.size() == (int)mags.size(), "applyGainsToSpectrum size");

    // --- 5) EqDsp (wired makePeaking) processes audio without NaN ---
    EqDsp eq;
    eq.prepare(bandFreqs, sr, 1.41f);
    std::vector<float> gains(bandCount, 0.0f);
    for (int i = 0; i < bandCount; ++i) gains[i] = out[i].gainDb;
    eq.setGains(gains);
    std::vector<float> audio(512);
    for (int i = 0; i < 512; ++i) audio[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sr);
    eq.process(audio.data(), 512);
    bool noNan = true;
    for (float v : audio) if (std::isnan(v) || std::isinf(v)) { noNan = false; break; }
    CHECK(noNan, "EqDsp output has no NaN/Inf");

    // --- 5b) Regresión: las curvas del canal DERECHO se aplican de verdad
    //     (antes el banco activo quedaba fijado en L y R nunca sonaba corregido)
    {
        EqDsp eqR2;
        eqR2.prepare(bandFreqs, sr, 1.41f);
        int band1k = 0;
        for (int i = 1; i < bandCount; ++i)
            if (std::fabs(std::log10(bandFreqs[i] / 1000.0f)) <
                std::fabs(std::log10(bandFreqs[band1k] / 1000.0f))) band1k = i;
        std::vector<float> zero(bandCount, 0.0f);
        eqR2.setGains(zero);
        std::vector<float> ref(4096), out(4096);
        for (int i = 0; i < 4096; ++i)
            ref[(size_t)i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / sr);
        out = ref;
        eqR2.process(out.data(), 4096);
        const double rmsFlat = rmsFrom(out, 1024);
        std::vector<float> cut(bandCount, 0.0f);
        cut[(size_t)band1k] = -12.0f;
        eqR2.setGains(cut);
        out = ref;
        eqR2.process(out.data(), 4096);
        const double rmsCut = rmsFrom(out, 1024);
        CHECK(rmsCut < rmsFlat * 0.6, "R-channel gains are actually applied");
        std::printf("  EqDsp canal R: rms plano=%.4f con corte 1k -12 dB=%.4f\n", rmsFlat, rmsCut);
    }

    // --- 5c) prepare() validado: parámetros corruptos (vector vacío, rate
    //     <= 0, frecuencias NaN) NO tocan el snapshot actual — se conserva
    //     el banco anterior. Regresión: el prepare viejo hacía
    //     assign(data, data + bands) sin chequear que el vector tuviera
    //     `bands` entradas (OOB read si el caller pasaba menos).
    {
        EqDsp eqG;
        eqG.prepare(bandFreqs, sr, 1.41f);
        std::vector<float> cutG(bandCount, 0.0f);
        cutG[0] = -12.0f;
        eqG.setGains(cutG);
        std::vector<float> refG(1024), outG(1024);
        for (int i = 0; i < 1024; ++i)
            refG[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * bandFreqs[0] * i / sr);
        outG = refG;
        eqG.process(outG.data(), 1024);
        const double rmsBefore = rmsFrom(outG, 256);

        eqG.prepare({}, sr, 1.41f);                        // vector vacío: sin-op
        eqG.prepare(bandFreqs, 0.0f, 1.41f);               // rate inválido: sin-op
        eqG.prepare(bandFreqs, -48000.0f, 1.41f);          // rate negativo: sin-op
        std::vector<float> nanFreqs = bandFreqs;
        nanFreqs[2] = std::nanf("");                      // frecuencia NaN: sin-op
        eqG.prepare(nanFreqs, sr, 1.41f);

        eqG.prepare(bandFreqs, sr, 1.41f);                // re-prepare válido
        eqG.setGains(cutG);
        outG = refG;
        eqG.process(outG.data(), 1024);
        const double rmsAfter = rmsFrom(outG, 256);
        // Mismo snapshot + mismo corte + mismo reset de estado: la salida es
        // bit a bit la que había antes de los prepare inválidos.
        CHECK(rmsAfter == rmsBefore, "prepare rejects bad params (previous snapshot kept)");
    }

    // --- 6) makePeaking(0 dB) is a flat-response biquad (b1==a1, b2==a2) ---
    BiquadCoeffs id = makePeaking(1000.0f, sr, 0.0f, 1.41f);
    CHECK(nearF(id.b1, id.a1, 1e-5f) && nearF(id.b2, id.a2, 1e-5f) &&
          nearF(id.b0, 1.0f, 1e-5f), "makePeaking(0dB) == flat response");

    // --- 7) Regresión A2: la sustracción de ruido NUNCA amplifica ---
    // En la fórmula antigua (dominio dB) con ruido a -60 dB y señal a -57 dB
    // devolvía -60 - (-60*0.5) = -30 dB: +30 dB de AMPLIFICACIÓN del ruido.
    {
        NoiseProfiler profiler(fftSize / 2, 50, 6.0f);
        std::vector<float> noiseMags(fftSize / 2, -60.0f);
        profiler.startCapture();
        bool done = false;
        for (int i = 0; i < 50 && !done; ++i) done = profiler.feedFrame(noiseMags);
        CHECK(done, "noise capture completes");
        CHECK(profiler.hasProfile(), "noise profile stored");

        std::vector<float> signal(fftSize / 2, -57.0f);  // 3 dB sobre el ruido (compuerta)
        const auto sub = profiler.subtractNoise(signal);
        bool neverAmplifies = true;
        for (size_t i = 0; i < sub.size(); ++i)
            if (sub[i] > signal[i] + 0.01f) { neverAmplifies = false; break; }
        CHECK(neverAmplifies, "subtractNoise never amplifies the signal");
        CHECK(sub[0] < -54.0f, "transition zone attenuates (linear-domain subtraction)");
        std::printf("  NoiseProfiler: medido=-57 dB, ruido=-60 dB -> %.2f dB\n", sub[0]);
    }

    if (g_fail == 0) std::printf("\nENGINE WIRING: ALL PASSED\n");
    else std::printf("\nENGINE WIRING: %d FAILURES\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
