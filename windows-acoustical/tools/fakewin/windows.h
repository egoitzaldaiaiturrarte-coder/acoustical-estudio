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
typedef char* LPSTR;
typedef const char* LPCSTR;
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
#define GENERIC_WRITE      0x40000000
#define CREATE_ALWAYS      2
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
// CONTEXT (x64): solo los campos que usa la app; el real es mucho mas
// grande (incluye XMM/vector). Nombres y tipos REALES del SDK.
typedef struct _CONTEXT {
    uint64_t P1Home, P2Home, P3Home, P4Home, P5Home, P6Home, P7Home, P8Home;
    uint64_t ContextFlags;
    uint64_t MxCsr;
    uint64_t SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    uint64_t EFlags;
    uint64_t Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    uint64_t Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    uint64_t R8, R9, R10, R11, R12, R13, R14, R15;
    uint64_t Rip;
} CONTEXT;
typedef CONTEXT* PCONTEXT;
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
// --- Conversion de cadenas (redirect ASan de CrashFilter.h) ---
#define CP_UTF8 65001
int     WideCharToMultiByte(unsigned int CodePage, DWORD dwFlags,
                            LPCWSTR lpWideCharStr, int cchWideChar,
                            LPSTR lpMultiByteStr, int cbMultiByte,
                            LPCSTR lpDefaultChar, BOOL* lpUsedDefaultChar);
// --- Tiempo ---
void    GetLocalTime(LPSYSTEMTIME lpSystemTime);
// --- Filtro de excepciones (Main.cpp) ---
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter);
// --- Memoria de proceso (CrashFilter.h) ---
BOOL    ReadProcessMemory(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer,
                          SIZE_T nSize, SIZE_T* lpNumberOfBytesRead);
DWORD   GetLastError(void);
// Pseudo-handles: el SDK REAL declara las funciones (exportadas por
// kernel32) y define además una macro función que intercepta la llamada
// con paréntesis. Por eso en el SDK compilan AMBAS formas:
//   GetCurrentProcess()  → macro → (HANDLE)-1
//   GetCurrentProcess    → función de kernel32 → devuelve (HANDLE)-1
// CrashFilter.h usa el literal reinterpret_cast<HANDLE>(0xFFFF...); ambas
// declaraciones se mantienen aquí para ser fieles al SDK.
HANDLE  GetCurrentProcess(void);
#define GetCurrentProcess() ((HANDLE)(intptr_t)-1)
#ifdef __cplusplus
}
#endif
