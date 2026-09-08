// AcousticalBridge.cpp — driver ASIO virtual en modo usuario. Cubase 5 lo ve
// como interface de audio; el audio viaja por memoria compartida hacia/desde
// Acoustical Estudio, que lo procesa (los tres ecuas dinámicos) y lo saca por
// la tarjeta real. No necesita firma de kernel.
#include "asio.h"
#include "BridgeShared.h"

#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <timeapi.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <algorithm>
#include <new>
#include <atomic>

namespace {

class AcousticalBridge;

AcousticalBridge* g_instance = nullptr;
std::atomic<long> g_refCount{0};

class AcousticalBridge final : public IASIO {
public:
    AcousticalBridge() { g_refCount.fetch_add(1); }
    ~AcousticalBridge() { g_refCount.fetch_sub(1); }

    // === IUnknown ===
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IASIO)) {
            *ppv = static_cast<IASIO*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = --refCount_;
        if (r == 0) delete this;
        return r;
    }

    // === IASIO ===
    ASIOBool STDMETHODCALLTYPE init(void* /*sysHandle*/) override {
        strncpy_s(errorMessage_, "OK", _TRUNCATE);
        mapSharedMemory();
        return ASIOTrue;
    }

    void STDMETHODCALLTYPE getDriverName(char* name) override {
        strncpy_s(name, 32, kBridgeDriverName, _TRUNCATE);
    }

    long STDMETHODCALLTYPE getDriverVersion() override { return 100; }

    void STDMETHODCALLTYPE getErrorMessage(char* error) override {
        strncpy_s(error, 128, errorMessage_, _TRUNCATE);
    }

    ASIOError STDMETHODCALLTYPE start() override {
        if (!buffersCreated_) return ASE_NotPresent;
        running_ = true;
        if (shared_) shared_->appRunning = 1;
        StartWorker();
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE stop() override {
        running_ = false;
        if (shared_) shared_->appRunning = 0;
        StopWorker();
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE getChannels(long* in, long* out) override {
        if (!in || !out) return ASE_InvalidParameter;
        *in = bridge::kChannels;
        *out = bridge::kChannels;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE getLatencies(long* in, long* out) override {
        if (!in || !out) return ASE_InvalidParameter;
        *in = static_cast<long>(bufferSize_);
        *out = static_cast<long>(bufferSize_ * 2);
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE getBufferSize(long* minS, long* maxS, long* prefS,
                                              long* gran) override {
        if (!minS || !maxS || !prefS || !gran) return ASE_InvalidParameter;
        *minS = 64; *maxS = 2048; *prefS = 512; *gran = -1;  // potencias de dos
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE canSampleRate(ASIOSamples rate) override {
        switch (static_cast<int>(rate)) {
            case 44100: case 48000: case 88200: case 96000: return ASE_OK;
            default: return ASE_NoClock;
        }
    }

    ASIOError STDMETHODCALLTYPE getSampleRate(ASIOSamples* rate) override {
        if (!rate) return ASE_InvalidParameter;
        *rate = sampleRate_;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE setSampleRate(ASIOSamples rate) override {
        if (canSampleRate(rate) != ASE_OK) return ASE_NoClock;
        sampleRate_ = rate;
        if (shared_) shared_->sampleRate = static_cast<uint32_t>(rate);
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE getClockSources(ASIOClockSource* clocks,
                                                long* numSources) override {
        if (!clocks || !numSources) return ASE_InvalidParameter;
        strncpy_s(clocks[0].name, "Internal", _TRUNCATE);
        clocks[0].isCurrentSource = ASIOTrue;
        clocks[0].index = 0; clocks[0].associatedChannel = -1; clocks[0].associatedGroup = -1;
        *numSources = 1;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE setClockSource(long) override { return ASE_OK; }

    ASIOError STDMETHODCALLTYPE getSamplePosition(ASIOSamples* pos,
                                                  ASIOTimeStamp* ts) override {
        if (!pos || !ts) return ASE_InvalidParameter;
        *pos = samplesPlayed_;
        const unsigned long t = timeGetTime();
        ts->lo = t; ts->hi = 0;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE getChannelInfo(ASIOChannelInfo* info) override {
        if (!info) return ASE_InvalidParameter;
        info->group = 0;
        info->type = ASIOSTFloat32LSB;
        info->isActive = buffersCreated_ && info->channel < bridge::kChannels;
        strncpy_s(info->name, info->isInput
            ? (info->channel == 0 ? "Bridge In L" : "Bridge In R")
            : (info->channel == 0 ? "Bridge Out L" : "Bridge Out R"), _TRUNCATE);
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE createBuffers(ASIOBufferInfo* infos, long numChannels,
                                              long bufferSize,
                                              ASIOCallbacks* callbacks) override {
        if (!infos || !callbacks || numChannels <= 0) return ASE_InvalidParameter;
        if (!IsPowerOfTwo(bufferSize)) return ASE_InvalidMode;
        mapSharedMemory();
        bufferSize_ = bufferSize;
        callbacks_ = *callbacks;
        for (long i = 0; i < numChannels; ++i) {
            const long ch = infos[i].channelNum;
            if (ch < 0 || ch >= bridge::kChannels) return ASE_InvalidParameter;
            float* buf = new float[bufferSize * 2];   // doble buffer intercalado
            infos[i].buffers[0] = buf;
            infos[i].buffers[1] = buf + bufferSize;
        }
        channels_ = numChannels;
        bufferInfos_ = new ASIOBufferInfo[numChannels];
        std::memcpy(bufferInfos_, infos, sizeof(ASIOBufferInfo) * numChannels);
        buffersCreated_ = true;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE disposeBuffers() override {
        StopWorker();
        if (bufferInfos_) {
            for (long i = 0; i < channels_; ++i)
                delete[] static_cast<float*>(bufferInfos_[i].buffers[0]);
            delete[] bufferInfos_;
            bufferInfos_ = nullptr;
        }
        channels_ = 0;
        buffersCreated_ = false;
        running_ = false;
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE controlPanel() override {
        MessageBoxA(nullptr,
            "Acoustical Bridge\n\nEl audio viaja por memoria compartida hacia "
            "Acoustical Estudio, donde los tres ecuas dinámicos lo corrigen y "
            "sale por la tarjeta real.\n\nMuestreo y buffer se eligen en Cubase.",
            kBridgeDriverName, MB_OK | MB_ICONINFORMATION);
        return ASE_OK;
    }

    ASIOError STDMETHODCALLTYPE future(long, void*) override { return ASE_InvalidParameter; }
    ASIOError STDMETHODCALLTYPE outputReady() override { return ASE_OK; }

    // === Registro COM ===
    static HRESULT Register() {
        char path[MAX_PATH];
        GetModuleFileNameA(g_hModule, path, MAX_PATH);
        HKEY hAsio = nullptr, hClsid = nullptr, hInproc = nullptr;
        RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO\\Acoustical Bridge",
                        0, nullptr, 0, KEY_WRITE, nullptr, &hAsio, nullptr);
        if (hAsio) {
            RegSetValueExA(hAsio, "CLSID", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(kBridgeClsid),
                static_cast<DWORD>(strlen(kBridgeClsid) + 1));
            RegCloseKey(hAsio);
        }
        char clsidKey[128];
        sprintf_s(clsidKey, "CLSID\\%s", kBridgeClsid);
        RegCreateKeyExA(HKEY_CLASSES_ROOT, clsidKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &hClsid, nullptr);
        if (hClsid) {
            RegSetValueExA(hClsid, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(kBridgeDriverName),
                static_cast<DWORD>(strlen(kBridgeDriverName) + 1));
            char inproc[192];
            sprintf_s(inproc, "%s\\InprocServer32", clsidKey);
            RegCreateKeyExA(HKEY_CLASSES_ROOT, inproc, 0, nullptr, 0, KEY_WRITE,
                            nullptr, &hInproc, nullptr);
            if (hInproc) {
                RegSetValueExA(hInproc, nullptr, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(path),
                    static_cast<DWORD>(strlen(path) + 1));
                const char* threading = "Both";
                RegSetValueExA(hInproc, "ThreadingModel", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(threading),
                    static_cast<DWORD>(strlen(threading) + 1));
                RegCloseKey(hInproc);
            }
            RegCloseKey(hClsid);
        }
        return S_OK;
    }

    static HRESULT Unregister() {
        RegDeleteTreeA(HKEY_CLASSES_ROOT, (std::string("CLSID\\") + kBridgeClsid).c_str());
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO\\Acoustical Bridge");
        return S_OK;
    }

    static void SetModuleHandle(HMODULE h) { g_hModule = h; }

private:
    static bool IsPowerOfTwo(long v) { return v > 0 && (v & (v - 1)) == 0; }

    void mapSharedMemory() {
        if (shared_) return;
        hMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(bridge::kSharedBlockSize),
                                   bridge::kSharedMemoryName);
        if (!hMap_) return;
        shared_ = static_cast<bridge::BridgeHeader*>(
            MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, bridge::kSharedBlockSize));
        if (shared_) {
            if (shared_->magic != bridge::kMagic) {
                *shared_ = bridge::BridgeHeader{};
                shared_->sampleRate = sampleRate_;
                shared_->bufferSize = bufferSize_;
            }
            shared_->bufferSize = bufferSize_;
            shared_->sampleRate = sampleRate_;
        }
    }

    static DWORD WINAPI WorkerThunk(LPVOID self) {
        static_cast<AcousticalBridge*>(self)->Worker();
        return 0;
    }

    void StartWorker() {
        if (worker_) return;
        timeBeginPeriod(1);
        worker_ = CreateThread(nullptr, 0, WorkerThunk, this, 0, nullptr);
    }

    void StopWorker() {
        if (!worker_) return;
        WaitForSingleObject(worker_, 2000);
        CloseHandle(worker_);
        worker_ = nullptr;
        timeEndPeriod(1);
    }

    void Worker() {
        const double periodMs = bufferSize_ * 1000.0 / static_cast<double>(sampleRate_);
        const double kHz = periodMs;
        ULONGLONG next = GetTickCount64();
        long activeBuffer = 0;
        while (running_) {
            // Copia reproducción: app → buffers de salida de Cubase (con la app
            // parada, silencio para que el DAW siga avanzando sin atasco)
            for (long i = 0; i < channels_; ++i) {
                if (bufferInfos_[i].isInput) continue;
                auto* out = static_cast<float*>(bufferInfos_[i].buffers[activeBuffer]);
                FillFromPlaybackRing(i, out, bufferSize_);
            }
            // Copia captura: entradas de Cubase → anillo hacia la app
            for (long i = 0; i < channels_; ++i) {
                if (!bufferInfos_[i].isInput) continue;
                auto* in = static_cast<float*>(bufferInfos_[i].buffers[activeBuffer]);
                PushToCaptureRing(i, in, bufferSize_);
            }
            samplesPlayed_ += bufferSize_;
            // Aviso al host (Cubase)
            ASIOTimeStamp ts{timeGetTime(), 0};
            callbacks_.bufferSwitch(activeBuffer, ASIOTrue);
            activeBuffer ^= 1;
            next += static_cast<ULONGLONG>(kHz);
            const ULONGLONG now = GetTickCount64();
            if (next > now) Sleep(static_cast<DWORD>(next - now));
            else next = now;
        }
    }

    void FillFromPlaybackRing(long channel, float* dest, long frames) {
        if (!shared_) { std::fill_n(dest, frames, 0.0f); return; }
        const uint64_t w = shared_->playbackWriteIndex;
        volatile uint64_t& r = shared_->playbackReadIndex;
        if (w < r + static_cast<uint64_t>(frames)) {
            // Subrun (la app va por detrás): silencio y resincroniza al final
            std::fill_n(dest, frames, 0.0f);
            r = w;
            return;
        }
        const float* ring = bridge::playbackRing(shared_);
        for (long i = 0; i < frames; ++i) {
            const uint64_t idx = (r + i) % bridge::kRingFrames;
            dest[i] = ring[idx * bridge::kChannels + channel];
        }
        r += frames;
    }

    void PushToCaptureRing(long channel, const float* src, long frames) {
        if (!shared_) return;
        float* ring = bridge::captureRing(shared_);
        uint64_t w = shared_->captureWriteIndex;
        for (long i = 0; i < frames; ++i) {
            const uint64_t idx = w % bridge::kRingFrames;
            ring[idx * bridge::kChannels + channel] = src[i];
            ++w;
        }
        shared_->captureWriteIndex = w;
    }

    ULONG refCount_ = 1;
    char errorMessage_[128] = "OK";
    ASIOSamples sampleRate_ = 48000;
    long bufferSize_ = 512;
    long channels_ = 0;
    bool buffersCreated_ = false;
    volatile bool running_ = false;
    ASIOSamples samplesPlayed_ = 0;
    ASIOCallbacks callbacks_{};
    ASIOBufferInfo* bufferInfos_ = nullptr;
    HANDLE worker_ = nullptr;
    HANDLE hMap_ = nullptr;
    bridge::BridgeHeader* shared_ = nullptr;

    static HMODULE g_hModule;
};

HMODULE AcousticalBridge::g_hModule = nullptr;

// === Class factory mínima ===

class BridgeFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        if (!g_instance) g_instance = new (std::nothrow) AcousticalBridge();
        if (!g_instance) return E_OUTOFMEMORY;
        return g_instance->QueryInterface(riid, ppv);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

BridgeFactory g_factory;

CLSID BridgeClsid() {
    CLSID clsid{};
    CLSIDFromString(reinterpret_cast<LPCOLESTR>(
        L"{B7A9E3D1-2C45-4F0A-8D63-1E5A0B7C9F42}"), &clsid);
    return clsid;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) AcousticalBridge::SetModuleHandle(hModule);
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (rclsid != BridgeClsid()) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return S_FALSE; }

STDAPI DllRegisterServer() { return AcousticalBridge::Register(); }
STDAPI DllUnregisterServer() { return AcousticalBridge::Unregister(); }
