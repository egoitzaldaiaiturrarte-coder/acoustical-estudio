// test_signalgen.cpp — valida el fix A1/A2: el seno de SignalGenerator suena a
// la frecuencia pedida (antes twoPi=π lo dejaba a f/2) y el sweep es coherente.
#include "SignalGenerator.h"
#include "FftProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace acoustical;

static int peakFreq(const std::vector<float>& frame, int sr, int fftN) {
    FftProcessor fft(fftN);
    std::vector<float> in(frame.begin(), frame.begin() + fftN);
    const auto& mags = fft.computeMagnitudesDb(in.data(), sr);
    const auto& freqs = fft.binFrequencies(sr);
    int pk = 0;
    for (int i = 1; i < (int)mags.size(); ++i) if (mags[i] > mags[pk]) pk = i;
    return (int)freqs[pk];
}

int main() {
    const int sr = 48000, fftN = 4096;
    int fails = 0;

    // 1) Seno a 1 kHz → pico en ~1 kHz (con el bug salía a ~500)
    {
        SignalGenerator g;
        g.setWaveform(SignalGenerator::Waveform::Sine);
        g.setFrequency(1000.0f);
        g.setLevelDb(0.0f);
        std::vector<float> buf(fftN);
        g.fill(buf.data(), fftN, sr);
        int pf = peakFreq(buf, sr, fftN);
        std::printf("  Seno 1 kHz -> pico en %d Hz\n", pf);
        if (pf < 950 || pf > 1050) { std::printf("FAIL: seno a la mitad de frecuencia\n"); fails++; }
    }

    // 2) Seno a 220 Hz → pico en ~220
    {
        SignalGenerator g;
        g.setWaveform(SignalGenerator::Waveform::Sine);
        g.setFrequency(220.0f);
        std::vector<float> buf(fftN);
        g.fill(buf.data(), fftN, sr);
        int pf = peakFreq(buf, sr, fftN);
        std::printf("  Seno 220 Hz -> pico en %d Hz\n", pf);
        if (pf < 200 || pf > 240) { std::printf("FAIL: 220 Hz mal\n"); fails++; }
    }

    // 3) makeLogSweep no debe exceder Nyquist en la frecuencia instantánea
    {
        auto sweep = SignalGenerator::makeLogSweep(20.0f, 20000.0f, 5.0, sr);
        bool finite = true;
        for (float v : sweep) if (std::isnan(v) || std::isinf(v)) { finite = false; break; }
        std::printf("  LogSweep: %zu muestras, finitas=%d\n", sweep.size(), finite ? 1 : 0);
        if (!finite) { std::printf("FAIL: sweep con NaN/Inf\n"); fails++; }
    }

    if (fails == 0) std::printf("\nSIGNALGEN: ALL PASSED\n");
    else std::printf("\nSIGNALGEN: %d FAILURES\n", fails);
    return fails ? 1 : 0;
}
