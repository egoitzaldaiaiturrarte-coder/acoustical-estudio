// asio.h — declaraciones mínimas del SDK de ASIO (subconjunto suficiente para
// un driver virtual). ASIO es una marca de Steinberg Media Technologies GmbH.
#pragma once
#include <cstdint>

#ifdef _WIN32
#include <unknwn.h>
#endif

typedef int32_t ASIOSamples;
typedef int64_t ASIOTimeStampValue;

enum ASIOBool { ASIOFalse = 0, ASIOTrue = 1 };
enum ASIOError {
    ASE_OK = 0, ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000, ASE_HWMalfunction, ASE_InvalidParameter,
    ASE_InvalidMode, ASE_SPNotAdvanced, ASE_NoClock, ASE_NoMemory
};
enum ASIOSampleType {
    ASIOSTInt16LSB = 0, ASIOSTInt24LSB = 1, ASIOSTInt32LSB = 2,
    ASIOSTFloat32LSB = 13, ASIOSTFloat64LSB = 14
};

struct ASIOBufferInfo {
    bool isInput;
    long channelNum;
    void* buffers[2];
};

struct ASIOChannelInfo {
    long channel;
    bool isInput;
    bool isActive;
    long group;
    ASIOSampleType type;
    char name[32];
};

struct ASIOTimeStamp {
    unsigned long lo, hi;
};

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    bool isCurrentSource;
    char name[32];
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*asioMessage)(long selector, long value, void* message, double* opt);
    void (*bufferSwitchTimeInfo)(ASIOTimeStamp* params, long doubleBufferIndex,
                                 ASIOBool directProcess);
};

// Interfaz COM del driver (mismos orden de métodos que el SDK oficial)
struct __declspec(uuid("B7A9E3D1-2C45-4F0A-8D63-1E5A0B7C9F42")) IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* error) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize,
                                    long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSamples rate) = 0;
    virtual ASIOError getSampleRate(ASIOSamples* rate) = 0;
    virtual ASIOError setSampleRate(ASIOSamples rate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo* infos, long numChannels,
                                    long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};

// CLSID del Acoustical Bridge (debe coincidir con el instalador y el registro)
inline constexpr const char* kBridgeClsidString =
    "{9B4F7A32-6C1D-4E6A-9B2C-AcoustiCAL01}";
// CLSID real generado y fijo:
inline constexpr const char* kBridgeClsid =
    "{B7A9E3D1-2C45-4F0A-8D63-1E5A0B7C9F42}";
inline constexpr const char* kBridgeDriverName = "Acoustical Bridge";
