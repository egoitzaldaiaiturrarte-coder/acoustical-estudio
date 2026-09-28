// windows.h minimal: solo las API que usa la app (AsioBridgeClient.h y
// CrashFilter.h), con las firmas REALES de Win32, para syntax-check del
// camino #ifdef JUCE_WINDOWS / _WIN32 en Linux.
// Si una llamada no compila contra estas firmas, no compila contra el SDK real.
#pragma once
#include <stdint.h>

typedef void* HANDLE;
typedef void* HMODULE;          // == HINSTANCE
typedef int BOOL;
typedef long LONG;
typedef long HRESULT;
typedef uint32_t DWORD;
typedef uint64_t SIZE_T;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* LPSECURITY_ATTRIBUTES;
typedef DWORD* LPDWORD;
typedef void* LPOVERLAPPED;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define S_OK ((HRESULT)0L)
#define MAX_PATH 260

#define FILE_MAP_ALL_ACCESS 0x000F001F
#define PAGE_READWRITE 0x04
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// Acceso a fichero (CreateFileW)
#define FILE_READ_DATA     0x00000001
#define FILE_APPEND_DATA   0x00000004
#define FILE_SHARE_READ    0x00000001
#define FILE_SHARE_WRITE   0x00000002
#define OPEN_ALWAYS        4
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_END           2

// SetFilePointer
typedef struct _SYSTEMTIME {
    unsigned short wYear;
    unsigned short wMonth;
    unsigned short wDayOfWeek;
    unsigned short wDay;
    unsigned short wHour;
    unsigned short wMinute;
    unsigned short wSecond;
    unsigned short wMilliseconds;
} SYSTEMTIME;
typedef SYSTEMTIME* LPSYSTEMTIME;

// Excepciones
#define EXCEPTION_MAXIMUM_PARAMETERS 15
typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    LPVOID ExceptionAddress;
    DWORD NumberParameters;
    unsigned long long ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD;
typedef EXCEPTION_RECORD* PEXCEPTION_RECORD;
typedef void* PCONTEXT;
typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS;
typedef EXCEPTION_POINTERS* PEXCEPTION_POINTERS;
typedef LONG (WINAPI* LPTOP_LEVEL_EXCEPTION_FILTER)(PEXCEPTION_POINTERS);
#define EXCEPTION_EXECUTE_HANDLER 1

// shlobj.h: CSIDL_APPDATA
#define CSIDL_APPDATA 26

#ifdef __cplusplus
extern "C" {
#endif
// --- Mapping de memoria (AsioBridgeClient.h) ---
HANDLE  OpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
HANDLE  CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttribute,
                            DWORD flProtect, DWORD dwMaximumSizeHigh,
                            DWORD dwMaximumSizeLow, LPCWSTR lpName);
LPVOID  MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                      DWORD dwOffsetHigh, DWORD dwOffsetLow, SIZE_T dwNumberOfBytesToMap);
BOOL    UnmapViewOfFile(LPCVOID lpBaseAddress);
BOOL    CloseHandle(HANDLE hObject);
// --- Fichero (CrashFilter.h) ---
HANDLE  CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
DWORD   SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LONG* lpDistanceToMoveHigh,
                       DWORD dwMoveMethod);
BOOL    WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                  LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
// --- Tiempo ---
void    GetLocalTime(LPSYSTEMTIME lpSystemTime);
// --- Filtro de excepciones (Main.cpp) ---
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
#define GetCurrentProcess ((HANDLE)(intptr_t)-3)
#ifdef __cplusplus
}
#endif
