// objbase.h fake: la parte del COM que usa el driver ASIO — class factory,
// DllGetClassObject, CLSIDFromString y LPCOLESTR. Solo syntax-check del camino
// #ifdef _WIN32 en Linux (ver el header hermano unknwn.h).
#pragma once
#include <unknwn.h>

extern const IID IID_IClassFactory;

struct IClassFactory : public IUnknown {
    virtual HRESULT CreateInstance(IUnknown* outer, REFIID riid, void** ppvObject) = 0;
    virtual HRESULT LockServer(BOOL fLockServerToUse) = 0;
};

// STDAPI = "HRESULT __stdcall" en el SDK real; para el check de sintaxis
// basta el tipo de retorno (la llamada/definicion del .cpp lo usan asi).
#define STDAPI HRESULT

// OLECHAR/LPCOLESTR (wtypes.h en el SDK real): wchar_t en el target Windows.
typedef wchar_t OLECHAR;
typedef const OLECHAR* LPCOLESTR;

#ifdef __cplusplus
extern "C" {
#endif
HRESULT CLSIDFromString(LPCOLESTR str, CLSID* pclsid);
HRESULT DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
#ifdef __cplusplus
}
#endif
