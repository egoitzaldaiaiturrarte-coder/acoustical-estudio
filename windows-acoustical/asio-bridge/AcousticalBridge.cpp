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
#include <mutex>

namespace {

class AcousticalBridge;

AcousticalBridge* g_instance = nullptr;
std::atomic<long> g_refCount{0};

class AcousticalBridge final : public IASIO {
public:
    AcousticalBridge() { g_refCount.fetch_add(1); }
    ~AcousticalBridge() {
        // Teardown completo e idempotente. Antes solo se bajaba el refcount:
        // un host (p. ej. Cubase) que cierra el proyecto con el audio sonando
        // llamaba a Release() con el worker vivo y dejaba al hilo dereferneciando
        // `this` ya liberada → UAF dentro del DAW. Parar el worker (unido con
        // WaitForSingleObject en StopWorker) y soltar la memoria compartida antes
        // de borrar nos da un final limpio. StopWorker es seguro si el worker
        // nunca partió (guarda sobre worker_) y balancea el timer (ver helper).
        running_ = false;
        StopWorker();
        if (shared_) { ::UnmapViewOfFile(shared_); shared_ = nullptr; }
        if (hMap_)   { ::CloseHandle(hMap_);       hMap_   = nullptr; }
        g_refCount.fetch_sub(1);
    }

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
        // init() devuelve ASIOBool (no un ASIOError): el fallo se indica con
        // ASIOFalse y el mensaje descriptivo se expone por getErrorMessage().
        // Antes se llamaba a mapSharedMemory() sin comprobar y se devolvía
        // ASIOTrue con "OK" aunque la memoria no se hubiera mapeado: el driver
        // "aparecía" en Cubase sin audio.
        setErrorMessage("OK");
        if (!mapSharedMemory()) return ASIOFalse;
        return ASIOTrue;
    }

    void STDMETHODCALLTYPE getDriverName(char* name) override {
        strncpy_s(name, 32, kBridgeDriverName, _TRUNCATE);
    }

    long STDMETHODCALLTYPE getDriverVersion() override { return 100; }

    void STDMETHODCALLTYPE getErrorMessage(char* error) override {
        // errorMessage_ puede escribirse desde el hilo de audio (worker) al
        // detectar eventos (p. ej. app muerta) y se lee aquí desde el hilo de
        // control: se protege con mutex para no copiar el buffer a medio escribir.
        std::lock_guard<std::mutex> lk(errMutex_);
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
        // sampleRate es std::atomic en el header compartido: se escribe con
        // .store() (relaxed). El lado de la app lo leerá con .load().
        if (shared_) shared_->sampleRate.store(static_cast<uint32_t>(rate), std::memory_order_relaxed);
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

        // createBuffers de nuevo sobre buffers ya creados: disponer primero, no
        // pisar las allocaciones previas y fugarlas (bufferInfos_ se sobreescribía).
        if (buffersCreated_) disposeBuffers();

        // 1) Validar TODOS los canales ANTES de allocar nada. El bucle antiguo
        //    validaba dentro: si el canal 0 era válido y el 1 no, el buffer del
        //    0 (ya allocado) se fugaba al salir con error.
        for (long i = 0; i < numChannels; ++i) {
            const long ch = infos[i].channelNum;
            if (ch < 0 || ch >= bridge::kChannels) return ASE_InvalidParameter;
        }

        mapSharedMemory();   // asegura shared_; si falla, init ya avisó (el worker degrada a silencio)

        // 2) Allocar con nothrow: un bad_alloc no debe cruzar la frontera COM
        //    como excepción C++; el target no la maneja → terminate del DAW.
        bufferSize_ = bufferSize;
        for (long i = 0; i < numChannels; ++i) {
            float* buf = new (std::nothrow) float[bufferSize * 2];   // doble buffer
            if (!buf) {
                for (long j = 0; j < i; ++j)   // liberar lo ya allocado en este intento
                    delete[] static_cast<float*>(infos[j].buffers[0]);
                setErrorMessage("Fallo al allocar buffers ASIO (sin memoria)");
                return ASE_NoMemory;
            }
            infos[i].buffers[0] = buf;
            infos[i].buffers[1] = buf + bufferSize;
        }
        ASIOBufferInfo* infosCopy = new (std::nothrow) ASIOBufferInfo[numChannels];
        if (!infosCopy) {
            for (long i = 0; i < numChannels; ++i)
                delete[] static_cast<float*>(infos[i].buffers[0]);
            setErrorMessage("Fallo al allocar bufferInfos_ (sin memoria)");
            return ASE_NoMemory;
        }
        std::memcpy(infosCopy, infos, sizeof(ASIOBufferInfo) * numChannels);

        // Estado coherente: todo se fija al final, así un fallo intermedio deja
        // intacto el estado previo (que ya se dispuso al inicio).
        bufferInfos_    = infosCopy;
        callbacks_      = *callbacks;
        channels_       = numChannels;
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
        // Antes se descartaban los resultados de los RegCreateKeyExA y se devolvía
        // S_OK siempre: un regsvr32 sin elevación "funcionaba" en silencio. Ahora
        // se recoge el PRIMERO no-S_OK y se devuelve (sin permisos → fallo claro).
        HRESULT hr = S_OK;
        HKEY hAsio = nullptr, hClsid = nullptr, hInproc = nullptr;
        const HRESULT r1 = RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO\\Acoustical Bridge",
                        0, nullptr, 0, KEY_WRITE, nullptr, &hAsio, nullptr);
        if (r1 != S_OK) hr = r1;
        if (hAsio) {
            RegSetValueExA(hAsio, "CLSID", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(kBridgeClsid),
                static_cast<DWORD>(strlen(kBridgeClsid) + 1));
            RegCloseKey(hAsio);
        }
        char clsidKey[128];
        sprintf_s(clsidKey, "CLSID\\%s", kBridgeClsid);
        const HRESULT r2 = RegCreateKeyExA(HKEY_CLASSES_ROOT, clsidKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &hClsid, nullptr);
        if (r2 != S_OK && hr == S_OK) hr = r2;
        if (hClsid) {
            RegSetValueExA(hClsid, nullptr, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(kBridgeDriverName),
                static_cast<DWORD>(strlen(kBridgeDriverName) + 1));
            char inproc[192];
            sprintf_s(inproc, "%s\\InprocServer32", clsidKey);
            const HRESULT r3 = RegCreateKeyExA(HKEY_CLASSES_ROOT, inproc, 0, nullptr, 0, KEY_WRITE,
                            nullptr, &hInproc, nullptr);
            if (r3 != S_OK && hr == S_OK) hr = r3;
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
        return hr;
    }

    static HRESULT Unregister() {
        // RegDeleteTreeA borra la rama y todos sus subnodos (RegDeleteKeyA fallaría
        // si hubiera subclaves) y su resultado YA NO se descarta: se devuelve el
        // primero no-S_OK en vez de S_OK siempre.
        HRESULT hr = S_OK;
        const HRESULT h1 = RegDeleteTreeA(HKEY_CLASSES_ROOT,
            (std::string("CLSID\\") + kBridgeClsid).c_str());
        if (h1 != S_OK) hr = h1;
        const HRESULT h2 = RegDeleteTreeA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\ASIO\\Acoustical Bridge");
        if (h2 != S_OK && hr == S_OK) hr = h2;
        return hr;
    }

    static void SetModuleHandle(HMODULE h) { g_hModule = h; }

private:
    static bool IsPowerOfTwo(long v) { return v > 0 && (v & (v - 1)) == 0; }

    bool mapSharedMemory() {
        if (shared_) return true;   // ya mapeada
        // Nombre GLOBAL (ver BridgeShared.h): la MISMA sección para todas las
        // sesiones de Windows. Sufijo por máquina (8 hex del MachineGuid) como
        // disyuntivo extra; si no se puede leer, se usa la base sin sufijo.
        const std::wstring name = bridge::makeSharedName(readMachineGuid());
        hMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   static_cast<DWORD>(bridge::kSharedBlockSize),
                                   name.c_str());
        if (!hMap_) {
            setErrorMessage("No se pudo crear/abrir la sección de memoria compartida (CreateFileMappingW)");
            return false;
        }
        shared_ = static_cast<bridge::BridgeHeader*>(
            MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, bridge::kSharedBlockSize));
        if (!shared_) {
            ::CloseHandle(hMap_); hMap_ = nullptr;
            setErrorMessage("No se pudo mapear la vista de la memoria compartida (MapViewOfFile)");
            return false;
        }
        if (shared_->magic.load(std::memory_order_acquire) != bridge::kMagic) {
            // Sección recién creada (memoria zero / magia inválida): la inicializamos
            // campo a campo y publicamos nuestra identidad. NO `*shared_ = {}` porque
            // el header ya no es copiable (contiene std::atomic).
            myInstanceId_ = makeInstanceId();
            bridge::initializeHeader(shared_, myInstanceId_);
        }
        // Sincronizamos con el header los parámetros que la app lee. Son
        // std::atomic, así que se escriben con .store() (relaxed).
        shared_->sampleRate.store(static_cast<uint32_t>(sampleRate_), std::memory_order_relaxed);
        shared_->bufferSize.store(static_cast<uint32_t>(bufferSize_), std::memory_order_relaxed);
        return true;
    }

    static DWORD WINAPI WorkerThunk(LPVOID self) {
        static_cast<AcousticalBridge*>(self)->Worker();
        return 0;
    }

    void StartWorker() {
        if (worker_) return;
        enterTimerPeriod();   // timeBeginPeriod(1); balanceado (ver el helper)
        worker_ = CreateThread(nullptr, 0, WorkerThunk, this, 0, nullptr);
    }

    void StopWorker() {
        if (!worker_) return;
        WaitForSingleObject(worker_, 2000);
        CloseHandle(worker_);
        worker_ = nullptr;
        leaveTimerPeriod();   // timeEndPeriod(1); balanceado (ver el helper)
    }

    void Worker() {
        // Pacing con QueryPerformanceCounter (QPC). El pacing anterior dormía a
        // ciegas sobre GetTickCount64 (resolución 1ms) con `next += period;
        // Sleep(next-now)`: arrastraba ~1ms de error por periodo (≈48 muestras a
        // 48k), que Cubase oye como clics intermitentes. Ahora espero al BORDE del
        // periodo: duermo la mayor parte (Sleep, con el timer a 1ms gracias a
        // timeBeginPeriod(1) vía el helper balanceado) y hago spin fino del último
        // ~1ms. Como siempre apunto a un borde ABSOLUTO (qpcNext += period), el
        // error no se acumula: queda acotado a la cuantización de 1ms. Fallback a
        // GetTickCount64 si QPC no estuviera disponible (no suele pasar).
        LARGE_INTEGER qpcFreq{};
        const bool useQpc = QueryPerformanceFrequency(&qpcFreq) && qpcFreq.QuadPart > 0;
        LARGE_INTEGER qpcPeriod{}, qpcNext{};
        if (useQpc) {
            qpcPeriod.QuadPart = static_cast<LONGLONG>(periodMs() * qpcFreq.QuadPart / 1000.0);
            if (qpcPeriod.QuadPart <= 0) qpcPeriod.QuadPart = 1;
            QueryPerformanceCounter(&qpcNext);
            qpcNext.QuadPart += qpcPeriod.QuadPart;
        }
        ULONGLONG nextTick = 0;
        if (!useQpc) nextTick = GetTickCount64();
        long activeBuffer = 0;

        while (running_) {
            // --- Heartbeat del driver (la app lo lee para saber que sigo vivo) ---
            if (shared_) shared_->tick++;

            // --- Vivacidad de la app ---
            // La app incrementa shared_->appTick en cada pullCapture/pushPlayback
            // (frente 4B). La sigo aquí: si deja de avanzar por más de ~5 periodos,
            // declaro la app muerta → congelo el playback (silencio) y lo dejo
            // visible en el header (kStatusAppDead) y en getErrorMessage().
            if (shared_) {
                const long long cur = shared_->appTick;
                if (cur != lastAppTick_) {
                    lastAppTick_ = cur;
                    lastAppTickAtMs_ = GetTickCount64();
                }
                const bool alive = appAlive();
                if (alive && appDead_) { appDead_ = false; clearAppDead(); }
                if (!alive && !appDead_) { appDead_ = true; setAppDead(); }
            }
            const bool serve = (shared_ && appAlive());   // ¿seguir sirviendo playback o congelar?

            // Contrato ASIO en buffers[activeBuffer]:
            //  - Salidas (isInput=false, "Bridge Out"): ahí renderiza CUBASE;
            //    el dispositivo las LEE. Se envían al capture ring (Cubase →
            //    app). (Contrato no tocado: quedó corregido en una auditoría previa.)
            for (long i = 0; i < channels_; ++i) {
                if (bufferInfos_[i].isInput) continue;
                auto* out = static_cast<float*>(bufferInfos_[i].buffers[activeBuffer]);
                PushToCaptureRing(i, out, bufferSize_);
            }
            //  - Entradas (isInput=true, "Bridge In"): el dispositivo las
            //    ESCRIBE y Cubase las lee. Se rellenan desde el playback ring
            //    (app → Cubase: señal de prueba). Si la app está muerta, el anillo
            //    está podrido: se congela (silencio) en vez de sonar datos obsoletos.
            for (long i = 0; i < channels_; ++i) {
                if (!bufferInfos_[i].isInput) continue;
                auto* in = static_cast<float*>(bufferInfos_[i].buffers[activeBuffer]);
                if (serve) FillFromPlaybackRing(i, in, bufferSize_);
                else          std::fill_n(in, bufferSize_, 0.0f);
            }
            samplesPlayed_ += bufferSize_;
            // Aviso al host (Cubase)
            ASIOTimeStamp ts{timeGetTime(), 0};
            callbacks_.bufferSwitch(activeBuffer, ASIOTrue);
            activeBuffer ^= 1;

            // --- Pacing: esperar al borde del siguiente periodo ---
            if (useQpc) {
                LARGE_INTEGER now{};
                const LONGLONG spinTicks = qpcFreq.QuadPart / 1000;   // ~1ms en ticks
                for (;;) {
                    QueryPerformanceCounter(&now);
                    const LONGLONG remain = qpcNext.QuadPart - now.QuadPart;
                    if (remain <= spinTicks) break;   // ya en la ventana final (~1ms)
                    const LONGLONG ms = remain / (qpcFreq.QuadPart / 1000);
                    Sleep(ms > 1 ? static_cast<DWORD>(ms) : 1);
                }
                for (;;) {   // spin fino hasta el borde (el 1ms del timer ya va activo)
                    QueryPerformanceCounter(&now);
                    if (now.QuadPart >= qpcNext.QuadPart) break;
                }
                qpcNext.QuadPart += qpcPeriod.QuadPart;
            } else {
                nextTick += static_cast<ULONGLONG>(periodMs());
                const ULONGLONG now = GetTickCount64();
                if (nextTick > now) Sleep(static_cast<DWORD>(nextTick - now));
                else nextTick = now;
            }
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

    // === Helpers de nombre / identidad / vivacidad / timer (front 4A) ===

    // Periodo de trabajo en ms: un bloque de bufferSize_ muestras a sampleRate_.
    // Lo usa el pacing QPC del worker y el umbral de "app muerta" (≈5× periodo).
    double periodMs() const {
        const double sr = (sampleRate_ > 0) ? static_cast<double>(sampleRate_) : 48000.0;
        return static_cast<double>(bufferSize_) * 1000.0 / sr;
    }

    // ¿La app sigue viva? VIVA si su último heartbeat (shared_->appTick, que la app
    // incrementa en cada pullCapture/pushPlayback — frente 4B) llegó hace menos de
    // ≈5 periodos. Si nunca ha llegado ninguno (appTick==0) se asume viva: la app
    // todavía no empezó a hacer pull/push.
    bool appAlive() const {
        if (!shared_ || shared_->appTick == 0) return true;
        if (lastAppTickAtMs_ == 0) return true;
        return (GetTickCount64() - lastAppTickAtMs_) <= appDeadThresholdMs();
    }
    // Milisegundos transcurridos desde la última actividad registrada de la app.
    long long lastAppActivityMs() const {
        if (!shared_ || shared_->appTick == 0 || lastAppTickAtMs_ == 0) return 0;
        return static_cast<long long>(GetTickCount64() - lastAppTickAtMs_);
    }
    ULONGLONG appDeadThresholdMs() const {
        // ≈5× periodo (task); suelo de 20ms para no declarar muerta con jitter fino.
        ULONGLONG t = static_cast<ULONGLONG>(5.0 * periodMs() + 0.5);
        if (t < 20) t = 20;
        return t;
    }

    // El worker (hilo de audio) escribe el mensaje al detectar eventos y
    // getErrorMessage (hilo de control) lo lee; errMutex_ evita copiar un buffer
    // a medio escribir. (4 args: dest, size, src, count — firma estándar de strncpy_s.)
    void setErrorMessage(const char* msg) {
        std::lock_guard<std::mutex> lk(errMutex_);
        strncpy_s(errorMessage_, 128, msg, _TRUNCATE);
    }

    // El driver detectó que la app dejó de avisar: congela el playback (lo hace el
    // worker) y deja visible el estado en el header (kStatusAppDead, lo lee el
    // cliente) y en getErrorMessage() (lo lee el DAW). Solo se llama en el FLANCO
    // (transición vivo↔muerto), no cada periodo.
    void setAppDead() {
        std::lock_guard<std::mutex> lk(errMutex_);
        if (shared_) shared_->statusFlags |= bridge::kStatusAppDead;
        strncpy_s(errorMessage_, 128,
            "Acoustical Estudio no avisa (app muerta): playback congelado", _TRUNCATE);
    }
    void clearAppDead() {
        std::lock_guard<std::mutex> lk(errMutex_);
        if (shared_) shared_->statusFlags &= ~bridge::kStatusAppDead;
        strncpy_s(errorMessage_, 128, "OK", _TRUNCATE);
    }

    // Identidad del extremo que CREA la sección: se publica en shared_->instanceId
    // (mapSharedMemory). El otro extremo la lee; si tras unos segundos de magia
    // válida ve un instanceId ajeno que no reconoce, puede avisar en UI "el driver
    // parece estar en otra sesión" (lo pinta la app, frente 4B). Distinto en cada
    // lado: GetTickCount64() * 100000 + GetProcessId().
    static uint64_t makeInstanceId() {
        return (GetTickCount64() * 100000ull) + static_cast<uint64_t>(GetProcessId());
    }

    // MachineGuid (HKLM\SOFTWARE\Microsoft\Cryptography): base del sufijo ESTABLE
    // del nombre de la sección (ver BridgeShared.h / makeSharedName). Devuelve "" si
    // no se puede leer: entonces el nombre cae a la base (sin sufijo) y la validación
    // cruzada de instanceId pilla la divergencia. La app debe hacer lo MISMO para
    // que ambos caigan en el mismo objeto.
    std::wstring readMachineGuid() {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                          0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return L"";
        }
        wchar_t buf[128] = {0};
        DWORD size = sizeof(buf), type = 0;
        const LSTATUS st = RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type,
                                             reinterpret_cast<BYTE*>(buf), &size);
        RegCloseKey(hKey);
        if (st != ERROR_SUCCESS || type != REG_SZ) return L"";
        return std::wstring(buf);   // null-terminado; el constructor copia hasta el \0
    }

    // Pareja timeBeginPeriod(1)/timeEndPeriod(1) balanceada en TODAS las salidas
    // (start/stop/dispose/destructor). El refcount garantiza que el timer del
    // sistema no quede desbalanceado aunque alguna ruta pare el worker "por otro
    // camino": solo se entra/sale del timer en la transición 0↔1.
    void enterTimerPeriod() {
        if (timerPeriods_.fetch_add(1, std::memory_order_relaxed) == 0) timeBeginPeriod(1);
    }
    void leaveTimerPeriod() {
        if (timerPeriods_.fetch_sub(1, std::memory_order_relaxed) == 1) timeEndPeriod(1);
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

    // --- Estado de vivacidad / nombre / timer (front 4A) ---
    std::mutex errMutex_;               // protege errorMessage_ y el RMW de statusFlags
    std::atomic<int> timerPeriods_{0};  // balancea timeBeginPeriod/timeEndPeriod(1)
    uint64_t myInstanceId_ = 0;         // mi identidad, si SOY yo quien creó la sección
    long long lastAppTick_ = 0;        // último appTick visto por el worker
    ULONGLONG lastAppTickAtMs_ = 0;    // GetTickCount64 del último cambio de appTick
    volatile bool appDead_ = false;    // ¿la app dejó de avisar? (edge, no cada periodo)

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

STDAPI DllCanUnloadNow() {
    // S_OK solo si nadie tiene referencias al driver (g_refCount==0). Antes
    // devolvía S_FALSE siempre: la DLL no se soltaba aunque no la usara nadie.
    return (g_refCount.load() == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() { return AcousticalBridge::Register(); }
STDAPI DllUnregisterServer() { return AcousticalBridge::Unregister(); }
