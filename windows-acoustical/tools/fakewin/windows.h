// windows.h minimal: solo las API que usa AsioBridgeClient.h, con las
// firmas REALES de Win32, para syntax-check del camino #ifdef _WIN32 en Linux.
// Si una llamada no compila contra estas firmas, no compila contra el SDK real.
#pragma once
#include <stdint.h>

typedef void* HANDLE;
typedef int BOOL;
typedef uint32_t DWORD;
typedef uint64_t SIZE_T;
typedef const wchar_t* LPCWSTR;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* LPSECURITY_ATTRIBUTES;
#define TRUE 1
#define FALSE 0

#define FILE_MAP_ALL_ACCESS 0x000F001F
#define PAGE_READWRITE 0x04
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

#ifdef __cplusplus
extern "C" {
#endif
// BOOL OpenFileMappingW? No: HANDLE.
HANDLE  OpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
HANDLE  CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttribute,
                           DWORD flProtect, DWORD dwMaximumSizeHigh,
                           DWORD dwMaximumSizeLow, LPCWSTR lpName);
LPVOID  MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                      DWORD dwOffsetHigh, DWORD dwOffsetLow, SIZE_T dwNumberOfBytesToMap);
BOOL    UnmapViewOfFile(LPCVOID lpBaseAddress);
BOOL    CloseHandle(HANDLE hObject);
#ifdef __cplusplus
}
#endif
