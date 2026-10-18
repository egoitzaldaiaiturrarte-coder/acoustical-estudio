// unknwn.h fake: IUnknown + el mecanismo GUID/IID/CLSID con el que trabaja
// el driver ASIO (asio.h y AcousticalBridge.cpp). Solo para syntax-check en
// Linux del camino #ifdef _WIN32; las firmas equivalen a las del SDK real.
//
// El SDK real hace compilables comparaciones directas de IID/CLSID
// (`riid == IID_IUnknown`, `rclsid != BridgeClsid()` — ambas presentes desde
// v1.0.0 y compiladas en los releases hasta v1.7.0): este fake proporciona
// operator==/operator!= para que el check no false sobre código que MSVC
// acepta. Es una check de SINTAXIS, no del runtime COM.
#pragma once
#include <stdint.h>
#include <windows.h>

#ifndef _MSC_VER
#define STDMETHODCALLTYPE
#define __declspec(x)
#endif

typedef struct tagGUID {
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef const IID& REFIID;
typedef const CLSID& REFCLSID;
typedef unsigned long ULONG;

// (ver el comentario de arriba: el SDK real hace legal esta comparacion)
inline bool operator==(const GUID& a, const GUID& b) {
    for (unsigned i = 0; i < 16; ++i)
        if (((const unsigned char*)&a)[i] != ((const unsigned char*)&b)[i]) return false;
    return true;
}
inline bool operator!=(const GUID& a, const GUID& b) { return !(a == b); }

// __uuidof(T): en MSVC el compilador genera el GUID embebido con
// __declspec(uuid(...)) y devuelve un puntero; aqui un lvalue comun basta
// para validar la sintaxis de `riid == __uuidof(IASIO)`.
namespace { const GUID __fake_uuid_any{}; }
#define __uuidof(t) __fake_uuid_any

struct IUnknown {
    virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG   AddRef() = 0;
    virtual ULONG   Release() = 0;
};

extern const IID IID_IUnknown;
