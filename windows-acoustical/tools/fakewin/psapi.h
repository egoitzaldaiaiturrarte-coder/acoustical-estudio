// psapi.h minimal: solo las API que usa CrashFilter.h, con firmas reales de
// Win32, para syntax-check en Linux.
#pragma once
#include "windows.h"

typedef struct tagMODULEINFO {
    LPVOID lpBaseOfDll;
    DWORD  SizeOfImage;
    HMODULE hModule;
} MODULEINFO;
typedef MODULEINFO* LPMODULEINFO;

#ifdef __cplusplus
extern "C" {
#endif
BOOL  EnumProcessModules(HANDLE hProcess, HMODULE lphModule, DWORD dwSize,
                         LPDWORD lpdwNeeded);
BOOL  GetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo,
                           DWORD cb);
DWORD GetModuleBaseNameW(HANDLE hProcess, HMODULE hModule, LPWSTR lpBaseName,
                         DWORD nSize);
#ifdef __cplusplus
}
#endif
