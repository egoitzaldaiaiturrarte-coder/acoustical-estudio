// RouteHub.h — hub de sistema: mezcla unificada y multiruta simultánea.
// Todo lo que procesa Acoustical (salida principal) se replica hacia hasta 4
// salidas auxiliares (Bluetooth, HDMI, USB, móvil por USB…), cada una con su
// propio: ganancia, mute, inversión de fase, all-pass de alineación de fase,
// retardo en pasos de 0,01 ms y auto-alineación por correlación cruzada con
// un micrófono de referencia conectado a la entrada del PC.
// La ruta 0 es la salida principal y también admite fase/retardo/ganancia,
// de modo que cualquier pareja de salidas puede quedar alineada en tiempo y
// en fase entre sí.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class RouteHub {
public:
    static constexpr int NUM_ROUTES = 5;   // 0 = principal, 1..4 auxiliares
    static constexpr int MAX_DELAY_SAMPLES = 1 << 15;   // ~680 ms a 48 kHz
    static constexpr float MAX_DELAY_MS = 100.0f;

    // === Parámetros por ruta (atómicos: la UI escribe, el audio lee) ===
    struct RouteParams {
        std::atomic<bool> enabled{false};
        std::atomic<bool> mute{false};
        std::atomic<bool> phaseInvert{false};
        std::atomic<bool> allPassOn{false};
        std::atomic<float> allPassFreqHz{80.0f};
        std::atomic<float> gainDb{0.0f};
        std::atomic<int> delaySamples{0};
        std::atomic<float> delayMs_{0.0f};   // valor pedido, en ms (para la UI)
        std::atomic<float> level{0.0f};      // pico del último bloque (0..1)
    };

    struct RouteInfo {
        int index = 0;
        juce::String name;          // "Salida principal" o nombre del dispositivo
        bool open = false;
        double sampleRate = 0.0;
        juce::String error;
    };

    explicit RouteHub(juce::AudioDeviceManager& mainManager)
        : mainManager_(mainManager) {
        mainManager_.createAudioDeviceTypes(deviceTypes_);
        for (int i = 0; i < NUM_ROUTES; ++i) {
            params_[i] = std::make_unique<RouteParams>();
            proc_[i] = std::make_unique<RouteProcessor>();
        }
        params_[0]->enabled.store(true);
    }

    ~RouteHub() { closeAllRoutes(); }

    // === Hilo de audio principal (lo llama ConsoleComponent) ===

    void setStreamInfo(double sampleRate) {
        if (sampleRate <= 8000.0) return;
        const double prev = mainSampleRate_.exchange(sampleRate);
        if (std::fabs(prev - sampleRate) < 1.0) return;
        // La tasa cambió: reconvierte los retardos guardados en ms
        for (int i = 0; i < NUM_ROUTES; ++i) {
            const float ms = params_[i]->delayMs_.load();
            params_[i]->delaySamples.store(
                static_cast<int>(ms * 0.001 * sampleRate));
            proc_[i]->resetDelay();
        }
    }
    double mainSampleRate() const { return mainSampleRate_.load(); }

    // Procesa la ruta 0 sobre left/right (in situ) y alimenta los anillos de
    // las rutas auxiliares con la señal post-EQ (antes de la ruta 0). Cada
    // ruta auxiliar aplica sus propios parámetros en su hilo de audio.
    void processMaster(float* left, float* right, int n, const float* inputMono) {
        const double sr = mainSampleRate_.load();
        if (n <= 0) return;

        // Referencia y entrada para la auto-alineación (siempre, mismo reloj)
        if (static_cast<int>(refMono_.size()) < n) refMono_.resize(static_cast<size_t>(n));
        for (int s = 0; s < n; ++s)
            refMono_[static_cast<size_t>(s)] = 0.5f * (left[s] + right[s]);
        refRing_.write(refMono_.data(), n);
        if (inputMono != nullptr) inRing_.write(inputMono, n);

        // Fan-out hacia las rutas auxiliares abiertas y activadas (físicas o
        // de red: las de red no tienen dispositivo propio, sino un sink que
        // manda las muestras por UDP a los altavoces del móvil, M3)
        for (int i = 1; i < NUM_ROUTES; ++i) {
            auto& rt = routes_[i];
            if ((rt.manager != nullptr || rt.netSink != nullptr)
                && params_[i]->enabled.load())
                rt.ring.write(left, right, n);
        }

        // Ruta 0: silencio si otra ruta está en medición (solo de medida)
        const int aligning = aligningRoute_.load();
        if (aligning > 0) {
            for (int s = 0; s < n; ++s) { left[s] = 0.0f; right[s] = 0.0f; }
            params_[0]->level.store(0.0f);
            return;
        }
        proc_[0]->process(left, right, n, sr, *params_[0]);
    }

    // === Gestión de rutas auxiliares (hilo de UI) ===

    juce::StringArray availableOutputDevices() {
        juce::StringArray names;
        for (auto* type : deviceTypes_) {
            type->scanForDevices();
            names.addArray(type->getDeviceNames(true));
        }
        names.removeDuplicates(false);
        names.removeEmptyStrings();
        return names;
    }

    bool openRoute(int index, const juce::String& deviceName, juce::String& error) {
        if (index <= 0 || index >= NUM_ROUTES) return false;
        closeRoute(index);
        auto& rt = routes_[index];
        rt.manager = std::make_unique<juce::AudioDeviceManager>();
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.sampleRate = mainSampleRate_.load();
        if (setup.sampleRate < 8000.0) setup.sampleRate = 48000.0;
        const auto err = rt.manager->initialise(0, 2, nullptr, false, deviceName, &setup);
        if (err.isNotEmpty()) {
            error = err;
            rt.manager.reset();
            return false;
        }
        rt.auxCallback = std::make_unique<AuxCallback>(*this, index);
        rt.manager->addAudioCallback(rt.auxCallback.get());
        if (auto* dev = rt.manager->getCurrentAudioDevice()) {
            rt.openName = dev->getName();
            rt.openRate = dev->getCurrentSampleRate();
        } else {
            rt.openName = deviceName;
            rt.openRate = setup.sampleRate;
        }
        params_[index]->enabled.store(true);
        return true;
    }

    /** Abre la ruta como salida DE RED (M3): en vez de un dispositivo físico,
     *  un sink que recibe las muestras procesadas (fase/retardo/ganancia ya
     *  aplicados) cada 10 ms. Típicamente el sink las manda por UDP a los
     *  altavoces de un móvil (RemoteAudioLink::sendToPhone). */
    bool openNetworkRoute(int index, const juce::String& name,
                          std::function<void(const float*, const float*, int, double)> sink,
                          juce::String& error) {
        if (index <= 0 || index >= NUM_ROUTES) return false;
        if (sink == nullptr) { error = juce::String::fromUTF8("sin destino"); return false; }
        closeRoute(index);
        auto& rt = routes_[index];
        rt.netSink = std::move(sink);
        rt.netName = name;
        rt.openName = name;
        rt.openRate = mainSampleRate_.load() > 8000.0 ? mainSampleRate_.load() : 48000.0;
        params_[index]->enabled.store(true);
        rt.netRunning.store(true);
        rt.netThread = std::thread([this, index] { pumpNetworkRoute(index); });
        return true;
    }

    void closeRoute(int index) {
        if (index <= 0 || index >= NUM_ROUTES) return;
        auto& rt = routes_[index];
        if (rt.netRunning.load()) {
            rt.netRunning.store(false);
            if (rt.netThread.joinable()) rt.netThread.join();
        }
        rt.netSink = nullptr;
        rt.netName = {};
        if (rt.manager != nullptr) {
            if (rt.auxCallback != nullptr) rt.manager->removeAudioCallback(rt.auxCallback.get());
            rt.manager->closeAudioDevice();
            rt.manager.reset();
            rt.auxCallback.reset();
        }
        rt.openName = {};
        rt.openRate = 0.0;
        rt.ring.reset();
    }

    void closeAllRoutes() {
        for (int i = 1; i < NUM_ROUTES; ++i) closeRoute(i);
    }

    RouteInfo routeInfo(int index) const {
        RouteInfo info;
        info.index = index;
        if (index < 0 || index >= NUM_ROUTES) return info;
        if (index == 0) {
            info.name = "Salida principal";
            info.open = true;
            info.sampleRate = mainSampleRate_.load();
            return info;
        }
        const auto& rt = routes_[index];
        info.name = rt.openName;
        info.open = rt.manager != nullptr || rt.netSink != nullptr;
        info.sampleRate = rt.openRate;
        return info;
    }

    // === Parámetros (hilo de UI) ===

    void setRouteEnabled(int i, bool e) { params_[i]->enabled.store(e); }
    bool isRouteEnabled(int i) const { return params_[i]->enabled.load(); }
    void setRouteMute(int i, bool m) { params_[i]->mute.store(m); }
    bool isRouteMuted(int i) const { return params_[i]->mute.load(); }
    void setPhaseInvert(int i, bool inv) {
        params_[i]->phaseInvert.store(inv);
        proc_[i]->resetAllPass();
    }
    bool isPhaseInverted(int i) const { return params_[i]->phaseInvert.load(); }

    // All-pass de 2 secciones: compensa el giro de fase alrededor de la
    // frecuencia de cruce (útil al mezclar altavoces de distinto tipo).
    void setAllPass(int i, bool on, float freqHz) {
        params_[i]->allPassOn.store(on && freqHz > 20.0f && freqHz < 1000.0f);
        params_[i]->allPassFreqHz.store(juce::jlimit(20.0f, 1000.0f, freqHz));
        proc_[i]->resetAllPass();
    }
    bool isAllPassOn(int i) const { return params_[i]->allPassOn.load(); }
    float allPassFreq(int i) const { return params_[i]->allPassFreqHz.load(); }

    void setRouteGainDb(int i, float db) {
        params_[i]->gainDb.store(juce::jlimit(-60.0f, 12.0f, db));
    }
    float routeGainDb(int i) const { return params_[i]->gainDb.load(); }

    void setDelayMs(int i, float ms) {
        const double sr = mainSampleRate_.load();
        const float clamped = juce::jlimit(0.0f, MAX_DELAY_MS, ms);
        params_[i]->delaySamples.store(static_cast<int>(clamped * 0.001 * sr));
        params_[i]->delayMs_.store(clamped);
        proc_[i]->resetDelay();
    }
    float delayMs(int i) const { return params_[i]->delayMs_.load(); }

    float routeLevel(int i) const {
        return juce::jlimit(0.0f, 1.0f, params_[i]->level.load());
    }

    // === Auto-alineación (hilo de UI; necesita música sonando y un micro
    //     conectado a la entrada del PC escuchando las salidas) ===
    // Fases: si aún no hay línea base, primero se mide la ruta 0 (referencia)
    // y después la ruta elegida. El retardo relativo queda en la ruta que lo
    // necesite para que ambas lleguen a la vez al micro.

    // 0 = nada, 1 = midiendo ruta 0, 2 = midiendo la ruta elegida
    int alignmentStage() const { return alignStage_.load(); }
    int alignmentRoute() const { return alignRoute_.load(); }

    void startAlignment(int route) {
        if (route < 0 || route >= NUM_ROUTES) return;
        if (alignmentStage() != 0) cancelAlignment();
        alignRoute_.store(route);
        alignStage_.store(route == 0 || baselineLag_.load() < 0.0f ? 1 : 2);
        aligningRoute_.store(alignStage_.load() == 1 ? 0 : route);
        stageStartMs_.store(nowMs());
        setAlignmentError({});
    }

    // 0 = midiendo, 1 = listo (msOut = retardo aplicado), -1 = error
    int pollAlignment(int route, float& msOut) {
        msOut = 0.0f;
        if (alignStage_.load() == 0 || alignRoute_.load() != route) return -1;
        const long long elapsed = nowMs() - stageStartMs_.load();
        if (elapsed < 1800) return 0;

        const float lag = measureLagSamples();
        if (lag < 0.0f) {
            setAlignmentError(juce::String::fromUTF8("Sin señal en la entrada del PC"));
            finishAlignment();
            return -1;
        }

        if (alignStage_.load() == 1) {
            baselineLag_.store(lag);
            if (alignRoute_.load() == 0) {
                finishAlignment();
                return 1;   // línea base capturada
            }
            aligningRoute_.store(alignRoute_.load());
            alignStage_.store(2);
            stageStartMs_.store(nowMs());
            return 0;
        }

        // Ruta auxiliar: retardo relativo respecto a la línea base
        const double sr = mainSampleRate_.load();
        float extraMs = static_cast<float>((lag - baselineLag_.load()) / sr * 1000.0);
        int appliedRoute = alignRoute_.load();
        if (extraMs < 0.0f) {
            // La auxiliar llega antes: el retardo va a la ruta principal
            setDelayMs(0, -extraMs);
            setDelayMs(appliedRoute, 0.0f);
            msOut = -extraMs;
        } else {
            setDelayMs(appliedRoute, extraMs);
            msOut = extraMs;
        }
        setAlignmentError({});
        finishAlignment();
        return 1;
    }

    void cancelAlignment() {
        finishAlignment();
    }

    juce::String lastAlignmentError() const {
        const juce::CriticalSection::ScopedLockType lock(errorLock_);
        return lastError_;
    }
    bool hasBaseline() const { return baselineLag_.load() >= 0.0f; }

private:
    // === Anillo lock-free productor único / consumidor único ===
    struct Ring {
        static constexpr int CAP = 1 << 16;
        std::array<float, CAP> ch0{}, ch1{}, mono{};
        std::atomic<std::int64_t> w{0}, r{0};

        void write(const float* l, const float* rgt, int n) {
            std::int64_t wv = w.load(std::memory_order_relaxed);
            const std::int64_t rv = r.load(std::memory_order_relaxed);
            if (wv - rv + n > CAP) r.store(wv + n - CAP, std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) {
                const int idx = static_cast<int>((wv + i) & (CAP - 1));
                ch0[idx] = l[i];
                ch1[idx] = rgt[i];
                mono[idx] = 0.5f * (l[i] + rgt[i]);
            }
            w.store(wv + n, std::memory_order_release);
        }
        void write(const float* m, int n) {
            std::int64_t wv = w.load(std::memory_order_relaxed);
            const std::int64_t rv = r.load(std::memory_order_relaxed);
            if (wv - rv + n > CAP) r.store(wv + n - CAP, std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) {
                const int idx = static_cast<int>((wv + i) & (CAP - 1));
                ch0[idx] = m[i];
            }
            w.store(wv + n, std::memory_order_release);
        }
        int buffered() const {
            return static_cast<int>(w.load(std::memory_order_acquire)
                                  - r.load(std::memory_order_acquire));
        }
        // Copia n muestras al buffer de salida (las últimas escritas).
        void readLatest(float* d0, float* d1, int n) {
            const std::int64_t wv = w.load(std::memory_order_acquire);
            const std::int64_t rv = r.load(std::memory_order_acquire);
            const int avail = static_cast<int>(wv - rv);
            const int take = std::min(n, avail);
            for (int i = 0; i < take; ++i) {
                const std::int64_t src = wv - take + i;
                const int idx = static_cast<int>(src & (CAP - 1));
                if (d0) d0[i] = ch0[idx];
                if (d1) d1[i] = ch1[idx];
            }
            for (int i = take; i < n; ++i) { if (d0) d0[i] = 0; if (d1) d1[i] = 0; }
        }
        void reset() { r.store(w.load(), std::memory_order_relaxed); }
    };

    // === Procesado por ruta: retardo → all-pass → fase → ganancia ===
    struct RouteProcessor {
        std::array<float, MAX_DELAY_SAMPLES> delayL{}, delayR{};
        int delayPos = 0;
        float apXL[2] = {0, 0}, apYL[2] = {0, 0};
        float apXR[2] = {0, 0}, apYR[2] = {0, 0};

        void resetDelay() { delayPos = 0; delayL.fill(0.0f); delayR.fill(0.0f); }
        void resetAllPass() {
            apXL[0] = apXL[1] = apYL[0] = apYL[1] = 0.0f;
            apXR[0] = apXR[1] = apYR[0] = apYR[1] = 0.0f;
        }

        void process(float* L, float* R, int n, double sr, RouteParams& p) {
            const bool mute = p.mute.load();
            const bool inv = p.phaseInvert.load();
            const float gain = juce::Decibels::decibelsToGain(p.gainDb.load());
            const bool apOn = p.allPassOn.load();
            float c = 0.0f;
            if (apOn && sr > 8000.0) {
                const float t = std::tan(3.14159265358979f
                                       * p.allPassFreqHz.load() / static_cast<float>(sr));
                c = (1.0f - t) / (1.0f + t);
            }
            const int dly = std::min(p.delaySamples.load(), MAX_DELAY_SAMPLES - 1);
            float peak = 0.0f;

            for (int s = 0; s < n; ++s) {
                float dl = L[s], dr = R[s];
                if (dly > 0) {
                    const float bufL = delayL[delayPos], bufR = delayR[delayPos];
                    delayL[delayPos] = dl;
                    delayR[delayPos] = dr;
                    dl = bufL;
                    dr = bufR;
                }
                if (apOn) {
                    float yl = c * dl + apXL[0] - c * apYL[0];
                    apXL[0] = dl; apYL[0] = yl; dl = yl;
                    yl = c * dl + apXL[1] - c * apYL[1];
                    apXL[1] = dl; apYL[1] = yl; dl = yl;
                    float yr = c * dr + apXR[0] - c * apYR[0];
                    apXR[0] = dr; apYR[0] = yr; dr = yr;
                    yr = c * dr + apXR[1] - c * apYR[1];
                    apXR[1] = dr; apYR[1] = yr; dr = yr;
                }
                float outL = inv ? -dl : dl;
                float outR = inv ? -dr : dr;
                outL *= gain;
                outR *= gain;
                if (mute) { outL = 0.0f; outR = 0.0f; }
                L[s] = outL;
                R[s] = outR;
                peak = std::max(peak, std::max(std::fabs(outL), std::fabs(outR)));
                delayPos = (delayPos + 1) & (MAX_DELAY_SAMPLES - 1);
            }
            p.level.store(peak);
        }
    };

    // === Callback de una salida auxiliar (su propio dispositivo e hilo) ===
    class AuxCallback : public juce::AudioIODeviceCallback {
    public:
        AuxCallback(RouteHub& hub, int index) : hub_(hub), index_(index) {}

        void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
            outRate_ = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
            pos_ = 0.0;
        }

        void audioDeviceIOCallbackWithContext(const float* const*, int,
                                              float* const* output, int numOutputs,
                                              int numSamples,
                                              const juce::AudioIODeviceCallbackContext&) override {
            if (numSamples <= 0) return;
            const double inRate = hub_.mainSampleRate();
            const double outRate = outRate_ > 8000.0 ? outRate_ : inRate;
            const double ratio = outRate > 0.0 ? inRate / outRate : 1.0;
            const int needed = static_cast<int>(std::ceil(numSamples * ratio)) + 2;

            auto& params = *hub_.params_[index_];
            auto& ring = hub_.routes_[index_].ring;

            // Solo de medición: si otra ruta está alineándose, callar
            const int aligning = hub_.aligningRoute_.load();
            if (aligning >= 0 && aligning != index_) {
                for (int ch = 0; ch < numOutputs; ++ch)
                    if (output[ch]) std::fill(output[ch], output[ch] + numSamples, 0.0f);
                params.level.store(0.0f);
                return;
            }

            inL_.assign(static_cast<size_t>(std::max(needed, 64)), 0.0f);
            inR_.assign(static_cast<size_t>(std::max(needed, 64)), 0.0f);
            ring.readLatest(inL_.data(), inR_.data(), needed);

            // Control de deriva entre relojes de dispositivos distintos
            if (ring.buffered() < needed * 2) pos_ = 0.0;
            else if (ring.buffered() > Ring::CAP / 2) ring.reset();

            scratchL_.resize(static_cast<size_t>(numSamples));
            scratchR_.resize(static_cast<size_t>(numSamples));
            for (int s = 0; s < numSamples; ++s) {
                const double ip = pos_;
                const int i0 = static_cast<int>(ip);
                const float frac = static_cast<float>(ip - std::floor(ip));
                const int i1 = std::min(i0 + 1, needed - 1);
                if (i0 < needed) {
                    scratchL_[static_cast<size_t>(s)] =
                        inL_[static_cast<size_t>(i0)] * (1.0f - frac)
                      + inL_[static_cast<size_t>(i1)] * frac;
                    scratchR_[static_cast<size_t>(s)] =
                        inR_[static_cast<size_t>(i0)] * (1.0f - frac)
                      + inR_[static_cast<size_t>(i1)] * frac;
                } else {
                    scratchL_[static_cast<size_t>(s)] = 0.0f;
                    scratchR_[static_cast<size_t>(s)] = 0.0f;
                }
                pos_ += ratio;
            }

            hub_.proc_[index_]->process(scratchL_.data(), scratchR_.data(),
                                        numSamples, outRate, params);
            if (numOutputs > 0 && output[0])
                std::copy(scratchL_.begin(), scratchL_.end(), output[0]);
            if (numOutputs > 1 && output[1])
                std::copy(scratchR_.begin(), scratchR_.end(), output[1]);
            for (int ch = 2; ch < numOutputs; ++ch)
                if (output[ch]) std::copy(scratchR_.begin(), scratchR_.end(), output[ch]);
        }

        void audioDeviceStopped() override {}

    private:
        RouteHub& hub_;
        int index_;
        double outRate_ = 48000.0;
        double pos_ = 0.0;
        std::vector<float> inL_, inR_, scratchL_, scratchR_;
    };

    // === Hilo de bombeo de una ruta de red (M3) ===
    // Lee el anillo (señal post-EQ que mete processMaster), aplica los
    // parámetros de la ruta y manda tramas de 10 ms al sink (UDP al móvil).
    void pumpNetworkRoute(int index) {
        auto& rt = routes_[index];
        std::vector<float> L, R;
        while (rt.netRunning.load()) {
            const double rate = mainSampleRate_.load();
            if (rate <= 8000.0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            auto& ring = rt.ring;
            const int target = static_cast<int>(std::lround(rate * 0.010));   // 10 ms
            // Espera a tener la trama completa (a lo sumo 100 ms: si no hay
            // señal, se envía silencio para que el reloj del móvil no se quede).
            int waited = 0;
            while (ring.buffered() < target && waited < 100 && rt.netRunning.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                waited += 2;
            }
            if (!rt.netRunning.load()) break;
            if (ring.buffered() > Ring::CAP / 2) ring.reset();
            if (L.size() < static_cast<size_t>(target)) {
                L.resize(static_cast<size_t>(target));
                R.resize(static_cast<size_t>(target));
            }
            ring.readLatest(L.data(), R.data(), target);
            proc_[index]->process(L.data(), R.data(), target, rate, *params_[index]);
            if (rt.netSink) rt.netSink(L.data(), R.data(), target, rate);
        }
    }

    friend class AuxCallback;

    // === Auto-alineación: correlación cruzada referencia ↔ micro ===

    float measureLagSamples() {
        constexpr int WINDOW = 16384;          // muestras a la tasa principal
        constexpr int DECIM = 4;
        constexpr int N = WINDOW / DECIM;      // 4096 tras diezmado
        constexpr int LAG = 2400;              // ±9600 muestras ≈ ±200 ms

        refCopy_.resize(WINDOW);
        inCopy_.resize(WINDOW);
        {
            const std::int64_t wv = refRing_.w.load(std::memory_order_acquire);
            const std::int64_t rv = refRing_.r.load(std::memory_order_acquire);
            const int avail = static_cast<int>(wv - rv);
            const int take = std::min(WINDOW, avail);
            for (int i = 0; i < take; ++i)
                refCopy_[static_cast<size_t>(i)] =
                    refRing_.ch0[static_cast<int>((wv - take + i) & (Ring::CAP - 1))];
            for (int i = take; i < WINDOW; ++i) refCopy_[static_cast<size_t>(i)] = 0.0f;
        }
        {
            const std::int64_t wv = inRing_.w.load(std::memory_order_acquire);
            const std::int64_t rv = inRing_.r.load(std::memory_order_acquire);
            const int avail = static_cast<int>(wv - rv);
            const int take = std::min(WINDOW, avail);
            for (int i = 0; i < take; ++i)
                inCopy_[static_cast<size_t>(i)] =
                    inRing_.ch0[static_cast<int>((wv - take + i) & (Ring::CAP - 1))];
            for (int i = take; i < WINDOW; ++i) inCopy_[static_cast<size_t>(i)] = 0.0f;
        }

        // Diezmado (promedio de 4) para que quepa en un latido de UI
        refDec_.resize(N);
        inDec_.resize(N);
        for (int i = 0; i < N; ++i) {
            float a = 0.0f, b = 0.0f;
            for (int k = 0; k < DECIM; ++k) {
                a += refCopy_[static_cast<size_t>(i * DECIM + k)];
                b += inCopy_[static_cast<size_t>(i * DECIM + k)];
            }
            refDec_[static_cast<size_t>(i)] = a / DECIM;
            inDec_[static_cast<size_t>(i)] = b / DECIM;
        }

        double energyRef = 0.0, energyIn = 0.0;
        for (int i = 0; i < N; ++i) {
            energyRef += static_cast<double>(refDec_[static_cast<size_t>(i)])
                       * refDec_[static_cast<size_t>(i)];
            energyIn += static_cast<double>(inDec_[static_cast<size_t>(i)])
                      * inDec_[static_cast<size_t>(i)];
        }
        if (energyRef < 1e-6 || energyIn < 1e-6) return -1.0f;

        double bestCorr = 0.0;
        int bestLag = 0;
        bool found = false;
        for (int lag = -LAG; lag <= LAG; ++lag) {
            double corr = 0.0;
            int count = 0;
            for (int i = 0; i < N; ++i) {
                const int j = i + lag;
                if (j < 0 || j >= N) continue;
                corr += static_cast<double>(refDec_[static_cast<size_t>(i)])
                      * inDec_[static_cast<size_t>(j)];
                ++count;
            }
            if (count < N / 2) continue;
            corr /= count;
            if (!found || corr > bestCorr) {
                bestCorr = corr;
                bestLag = lag;
                found = true;
            }
        }
        if (!found) return -1.0f;
        return static_cast<float>(bestLag * DECIM);
    }

    void finishAlignment() {
        aligningRoute_.store(-1);
        alignStage_.store(0);
        alignRoute_.store(-1);
        for (int i = 0; i < NUM_ROUTES; ++i) routes_[i].ring.reset();
    }

    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // === Estado ===
    struct Route {
        std::unique_ptr<RouteParams> params;
        std::unique_ptr<RouteProcessor> proc;
        std::unique_ptr<juce::AudioDeviceManager> manager;
        std::unique_ptr<AuxCallback> auxCallback;
        Ring ring;
        juce::String openName;
        double openRate = 0.0;
        // Ruta de red (M3): sin dispositivo; un sink recibe las muestras
        // procesadas cada 10 ms (típicamente: UDP a los altavoces del móvil).
        std::function<void(const float*, const float*, int, double)> netSink;
        juce::String netName;
        std::thread netThread;
        std::atomic<bool> netRunning{false};
    };

    juce::AudioDeviceManager& mainManager_;
    juce::OwnedArray<juce::AudioIODeviceType> deviceTypes_;

    std::array<std::unique_ptr<RouteParams>, NUM_ROUTES> params_{};
    std::array<std::unique_ptr<RouteProcessor>, NUM_ROUTES> proc_{};
    std::array<Route, NUM_ROUTES> routes_{};

    std::atomic<double> mainSampleRate_{48000.0};

    // Alineación
    Ring refRing_, inRing_;
    std::vector<float> refMono_;
    std::vector<float> refCopy_, inCopy_, refDec_, inDec_;
    std::atomic<int> aligningRoute_{-1};
    std::atomic<int> alignStage_{0};
    std::atomic<int> alignRoute_{-1};
    std::atomic<long long> stageStartMs_{0};
    std::atomic<float> baselineLag_{-1.0f};

    void setAlignmentError(const juce::String& e) {
        const juce::CriticalSection::ScopedLockType lock(errorLock_);
        lastError_ = e;
    }
    mutable juce::CriticalSection errorLock_;
    juce::String lastError_;
};
