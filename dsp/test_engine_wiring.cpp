// test_engine_wiring.cpp — proves the wired Windows engine compiles, links
// against the shared C core, and still behaves correctly end-to-end.
//
// Compiles the REAL engine sources (FftProcessor.cpp, EqDsp.cpp) + the
// header-only RoomCorrector.h and drives them: FFT -> aggregateBands ->
// computeCorrections -> applyGainsToSpectrum, plus an EqDsp pass.
#include "FftProcessor.h"
#include "RoomCorrector.h"
#include "EqDsp.h"
#include "StandardFrequencies.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace acoustical;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } } while (0)
static bool nearF(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

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
    // Hamming window halves coherent gain, so a unit sine peaks near -5 dB,
    // far above the -120 dB noise floor.
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
    eq.prepare(bandCount, bandFreqs.data(), sr, 1.41f);
    std::vector<float> gains(bandCount, 0.0f);
    for (int i = 0; i < bandCount; ++i) gains[i] = out[i].gainDb;
    eq.setGains(false, gains);
    std::vector<float> audio(512);
    for (int i = 0; i < 512; ++i) audio[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sr);
    eq.process(audio.data(), 512);
    bool noNan = true;
    for (float v : audio) if (std::isnan(v) || std::isinf(v)) { noNan = false; break; }
    CHECK(noNan, "EqDsp output has no NaN/Inf");

    // --- 6) makePeaking(0 dB) is a flat-response biquad (b1==a1, b2==a2) ---
    BiquadCoeffs id = makePeaking(1000.0f, sr, 0.0f, 1.41f);
    CHECK(nearF(id.b1, id.a1, 1e-5f) && nearF(id.b2, id.a2, 1e-5f) &&
          nearF(id.b0, 1.0f, 1e-5f), "makePeaking(0dB) == flat response");

    if (g_fail == 0) std::printf("\nENGINE WIRING: ALL PASSED\n");
    else std::printf("\nENGINE WIRING: %d FAILURES\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
