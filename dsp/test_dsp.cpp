// test_dsp.cpp — Differential test: the ORIGINAL C++ engine (FftProcessor.cpp,
// RoomCorrector.h) vs the new shared C core (acoustical_dsp.c).
//
// Proves the C core is a drop-in: identical FFT magnitudes and identical band
// aggregation, plus correctness checks (sine peak location, biquad DC/center
// gain, aggregate on empty/noise).
#include "FftProcessor.h"
#include "RoomCorrector.h"
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);       \
        }                                                                      \
    } while (0)

static double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        m = std::max(m, (double)std::fabs(a[i] - b[i]));
    return m;
}

// ---- FFT: original vs C core, random + sine, several sizes ----
static void testFftEquivalence() {
    const int sizes[] = {512, 1024, 2048, 4096, 8192};
    const int sampleRate = 48000;
    for (int size : sizes) {
        acoustical::FftProcessor orig(size);
        acoustical_fft* c = acoustical_fft_create(size);
        CHECK(c != nullptr, "C fft created");
        if (!c) continue;
        CHECK(orig.binCount() == acoustical_fft_bin_count(c), "bin count matches");

        // Random input
        std::vector<float> in(size);
        for (int i = 0; i < size; ++i) in[i] = (float)(rand() / (double)RAND_MAX * 2.0 - 1.0);
        const std::vector<float>& o = orig.computeMagnitudesDb(in.data(), sampleRate);
        std::vector<float> cm(orig.binCount());
        acoustical_fft_compute_magnitudes_db(c, in.data(), sampleRate, cm.data());
        CHECK(maxAbsDiff(o, cm) < 1e-3, "FFT random magnitudes match (1e-3)");

        // Sine input at a known bin
        const int freq = 1000;
        for (int i = 0; i < size; ++i) in[i] = std::sin(2.0 * 3.141592653589793 * freq * i / sampleRate);
        const std::vector<float>& o2 = orig.computeMagnitudesDb(in.data(), sampleRate);
        std::vector<float> cm2(orig.binCount());
        acoustical_fft_compute_magnitudes_db(c, in.data(), sampleRate, cm2.data());
        CHECK(maxAbsDiff(o2, cm2) < 1e-3, "FFT sine magnitudes match (1e-3)");

        // Peak must be at the 1 kHz bin
        int peak = 0;
        for (int i = 1; i < orig.binCount(); ++i) if (cm2[i] > cm2[peak]) peak = i;
        const float binHz = (float)sampleRate / (float)size;
        const int expected = (int)std::lround((double)freq / binHz);
        CHECK(std::abs(peak - expected) <= 1, "sine peak at expected bin");

        // bin frequencies match
        const std::vector<float>& of = orig.binFrequencies(sampleRate);
        std::vector<float> cf(orig.binCount());
        acoustical_fft_bin_frequencies(c, sampleRate, cf.data());
        CHECK(maxAbsDiff(of, cf) < 1e-4, "bin frequencies match");

        acoustical_fft_free(c);
    }
}

// ---- FFT: pure tone magnitude ~ 0 dB for full-scale sine ----
static void testFftMagnitude() {
    const int size = 2048, sampleRate = 48000;
    acoustical_fft* c = acoustical_fft_create(size);
    std::vector<float> in(size);
    for (int i = 0; i < size; ++i) in[i] = std::sin(2.0 * 3.141592653589793 * 1000.0 * i / sampleRate);
    std::vector<float> mag(size / 2);
    acoustical_fft_compute_magnitudes_db(c, in.data(), sampleRate, mag.data());
    // A full-scale sine split across ~2 bins; the peak bin should be near 0 dB
    // (within a few dB, since the Hamming window spreads energy).
    float peak = -1e9;
    for (float v : mag) peak = std::max(peak, v);
    CHECK(peak > -12.0f && peak < 3.0f, "full-scale sine peak near 0 dB");
    acoustical_fft_free(c);
}

// ---- aggregateBands: original C++ vs C core ----
static void testAggregateEquivalence() {
    const int sampleRate = 48000;
    const int fftSize = 4096;
    const int binCount = fftSize / 2;
    const float noiseFloor = -120.0f;

    // Build standard band frequencies (use a log-spaced set, 124 bands > 40).
    std::vector<float> bandFreqs;
    {
        const double fmin = 20.0, fmax = 20000.0;
        for (int b = 0; b < 124; ++b) {
            double t = (double)b / 123.0;
            bandFreqs.push_back((float)(fmin * std::pow(fmax / fmin, t)));
        }
    }

    acoustical::RoomCorrector orig(bandFreqs, sampleRate, binCount, 12.0f, 0.3f, noiseFloor);

    std::vector<float> binFreqs(binCount);
    for (int i = 0; i < binCount; ++i) binFreqs[i] = (float)i * (float)sampleRate / (float)fftSize;

    std::vector<float> mags(binCount);
    // Random magnitudes with some structure.
    for (int i = 0; i < binCount; ++i)
        mags[i] = -120.0f + (float)(rand() / (double)RAND_MAX) * 100.0f;

    std::vector<float> o = orig.aggregateBands(mags, binFreqs);
    std::vector<float> c(bandFreqs.size());
    acoustical_aggregate_bands(bandFreqs.data(), (int)bandFreqs.size(), mags.data(), (int)mags.size(),
                               binFreqs.data(), (int)binFreqs.size(), noiseFloor, c.data());
    CHECK(maxAbsDiff(o, c) < 1e-3, "aggregateBands matches original (1e-3)");

    // Edge: magnitudes array SHORTER than bins (the i<magnitudesDb.size() guard).
    std::vector<float> shortMags(binCount / 2, -50.0f);
    std::vector<float> o2 = orig.aggregateBands(shortMags, binFreqs);
    std::vector<float> c2(bandFreqs.size());
    acoustical_aggregate_bands(bandFreqs.data(), (int)bandFreqs.size(), shortMags.data(),
                               (int)shortMags.size(), binFreqs.data(), (int)binFreqs.size(),
                               noiseFloor, c2.data());
    CHECK(maxAbsDiff(o2, c2) < 1e-3, "aggregateBands matches with short mags (1e-3)");

    // Edge: all-noise -> every band == noiseFloor.
    std::vector<float> allNoise(binCount, -150.0f);
    std::vector<float> o3 = orig.aggregateBands(allNoise, binFreqs);
    std::vector<float> c3(bandFreqs.size());
    acoustical_aggregate_bands(bandFreqs.data(), (int)bandFreqs.size(), allNoise.data(),
                               (int)allNoise.size(), binFreqs.data(), (int)binFreqs.size(),
                               noiseFloor, c3.data());
    CHECK(maxAbsDiff(o3, c3) < 1e-6, "aggregateBands matches all-noise");
    for (float v : c3) CHECK(std::abs(v - noiseFloor) < 1e-6, "all-noise band == noiseFloor");
}

// ---- Biquad: DC gain ~1 for peaking, center gain ~ target ----
static void testBiquad() {
    acoustical_biquad_coeffs c;
    acoustical_make_peaking(1000.0f, 48000.0f, 0.0f, 1.41f, &c);
    // Zero-gain peaking should be (near) an all-pass: DC and Nyquist gain ~ 1.
    float state[2] = {0, 0};
    float y = acoustical_biquad_process(&c, state, 1.0f);
    // After a few samples the transient settles; DC gain of 0 dB peaking = 1.
    for (int i = 0; i < 64; ++i) y = acoustical_biquad_process(&c, state, 1.0f);
    CHECK(std::fabs(y - 1.0f) < 1e-3, "0 dB peaking DC gain ~ 1");

    // +6 dB peaking at 1 kHz: drive a 1 kHz sine and measure steady amplitude.
    acoustical_make_peaking(1000.0f, 48000.0f, 6.0f, 1.41f, &c);
    state[0] = state[1] = 0;
    const int n = 4096;
    float peakOut = 0;
    for (int i = 0; i < n; ++i) {
        float x = std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
        float out = acoustical_biquad_process(&c, state, x);
        if (i > n / 2) peakOut = std::max(peakOut, std::fabs(out));
    }
    // 6 dB boost ~ x2 amplitude (1.0 -> ~2.0).
    CHECK(peakOut > 1.6f && peakOut < 2.4f, "+6 dB peaking center gain ~ 2x");

    // 0 dB gain peaking at center should pass a 1 kHz sine ~ unchanged.
    acoustical_make_peaking(1000.0f, 48000.0f, 0.0f, 1.41f, &c);
    state[0] = state[1] = 0;
    peakOut = 0;
    for (int i = 0; i < n; ++i) {
        float x = std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0);
        float out = acoustical_biquad_process(&c, state, x);
        if (i > n / 2) peakOut = std::max(peakOut, std::fabs(out));
    }
    CHECK(peakOut > 0.9f && peakOut < 1.1f, "0 dB peaking passes center sine ~1x");
}

// ---- C core: invalid sizes ----
static void testFftInvalid() {
    CHECK(acoustical_fft_create(0) == nullptr, "create(0) -> null");
    CHECK(acoustical_fft_create(100) == nullptr, "create(100) -> null (not power of 2)");
    CHECK(acoustical_fft_create(3) == nullptr, "create(3) -> null (odd)");
    CHECK(acoustical_fft_create(256) != nullptr, "create(256) -> ok");
    CHECK(acoustical_fft_create(1024) != nullptr, "create(1024) -> ok");
    CHECK(acoustical_fft_create(16384) != nullptr, "create(16384) -> ok (large)");
}

int main() {
    std::srand(12345);
    testFftEquivalence();
    testFftMagnitude();
    testAggregateEquivalence();
    testBiquad();
    testFftInvalid();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
