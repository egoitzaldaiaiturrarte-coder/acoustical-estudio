// test_engine_race.cpp — estrés del motor: reproduce el bug C1/C4
// (configure()/setBandGain()/resetBands() con el motor corriendo) y verifica
// que no haya UAF/carrera. Corre el motor real, alimenta audio y martillear
// la configuración desde otro hilo.
#include "AcousticalEngine.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace acoustical;

int main() {
    AcousticalEngine engine;
    long long analyses = 0;
    engine.onAnalysis = [&](const AnalysisResult& r) {
        (void)r;
        analyses++;
    };

    // Audio de entrada: seno 1 kHz + ruido, a 48 kHz (distinto de los 96 del
    // config por defecto → fuerza el re-prepare del EQ, ver A3).
    const int sr = 48000;
    const int block = 1024;
    std::vector<float> buf(block);
    double ph = 0.0;
    const double twoPi = 6.283185307179586;

    AudioConfig cfg;
    cfg.sampleRate = SampleRate::Hz48000;
    cfg.bandCount = BandCount::B31;
    cfg.fftSize = FftSize::S2048;
    engine.configure(cfg);
    engine.start();

    std::atomic<bool> stopPush{false};
    std::atomic<bool> stopCfg{false};
    int errors = 0;

    // Hilo que emula el callback de audio: pushSamples continuo.
    std::thread audio([&] {
        while (!stopPush.load()) {
            for (int i = 0; i < block; ++i) {
                buf[i] = (float)(0.5 * std::sin(ph) + 0.01 * (rand() % 1000 / 500.0 - 1.0));
                ph += twoPi * 1000.0 / sr;
            }
            engine.pushSamples(buf.data(), block, sr);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // Hilo que martillear la configuración con el motor en marcha (el bug).
    std::thread cfgThread([&] {
        BandCount counts[] = {BandCount::B10, BandCount::B31, BandCount::B16, BandCount::B10};
        for (int iter = 0; iter < 40 && !stopCfg.load(); ++iter) {
            AudioConfig c = cfg;
            c.bandCount = counts[iter % 4];
            c.maxGainDb = 6.0f + (iter % 5) * 3.0f;
            engine.configure(c);                       // recreate fft/corrector/sweepers en caliente
            for (int i = 0; i < 40; ++i) {
                engine.setBandGain(i, (float)(rand() % 24 - 12));  // escritura UI concurrente
            }
            if (iter % 7 == 0) engine.resetBands();
            if (iter % 5 == 0) engine.captureReference();
            if (iter % 3 == 0) {
                engine.setDynamicEqEnabled(0, true);
                engine.setDynamicEqMaxGain(1, 8.0f);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });

    // Deja correr ~2.5 s de estrés.
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    stopCfg.store(true);
    cfgThread.join();
    stopPush.store(true);
    audio.join();

    engine.stop();

    // Tras el estrés, el motor debe estar íntegro y responder.
    auto bands = engine.bands();
    if (bands.empty()) { std::printf("FAIL: bands vacías tras el estrés\n"); errors++; }
    long long after = analyses;
    if (after < 5) { std::printf("FAIL: apenas analizó (%lld)\n", (long long)after); errors++; }

    std::printf("  análisis producidos: %lld\n", (long long)after);
    std::printf("  bandas tras estrés: %zu\n", bands.size());
    if (errors == 0) std::printf("\nENGINE RACE STRESS: ALL PASSED\n");
    else std::printf("\nENGINE RACE STRESS: %d FAILURES\n", errors);
    return errors == 0 ? 0 : 1;
}
