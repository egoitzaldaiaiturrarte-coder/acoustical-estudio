// CrashFilter.h — filtro de excepciones fatal (solo Windows/MSVC), C puro.
//
// Si la app muere por un error fatal (p. ej. 0xC0000005 = acceso a memoria),
// Windows muestra su diálogo y el proceso muere. Para poder ver DÓNDE murió,
// anotamos en %AppData%\Acoustical\startup.log:
//   · el código de excepción y la dirección del fallo
//   · el MÓDULO (exe o DLL) que contiene esa dirección, con su nombre y el
//     desplazamiento desde su base (¿es nuestra app o un driver/librería?)
//   · en un 0xC0000005, si era lectura o escritura y la dirección afectada
//   · diagnóstico fino: registros RIP/RSP/RBP, los bytes en la dirección del
//     fallo (¿código real o memoria salvaje?), 128 B de pila alrededor de
//     RSP y, si la dirección no caía en ninguna imagen cargada, la lista de
//     módulos (nombre base+tamaño) para verificar la enumeración y obtener
//     la base del exe (offset = dirección - base → symbolizar con el PDB)
//
// Todo con Win32 puro (sin JUCE ni std): a esa altura no se puede fiar de
// nada. El archivo se valida en CI contra estas firmas reales
// (tools/fakewin/).
#pragma once

#ifdef JUCE_WINDOWS
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <cwchar>
#include <cstdio>

namespace crashfilter {

// Pseudo-handle del proceso actual, literal: no depende de la macro
// GetCurrentProcess (su definición cambia entre SDKs: con o sin
// paréntesis). El valor documentado del pseudo-handle es
// (HANDLE)0xFFFFFFFFFFFFFFFF (igual que INVALID_HANDLE_VALUE).
inline HANDLE procHandle() { return reinterpret_cast<HANDLE>(0xFFFFFFFFFFFFFFFFull); }

inline void append(const wchar_t* line) {
    wchar_t base[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base) != S_OK)
        return;
    wchar_t full[MAX_PATH] = {};
    swprintf(full, MAX_PATH, L"%s\\Acoustical\\startup.log", base);
    HANDLE h = CreateFileW(full, FILE_APPEND_DATA | FILE_READ_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(wcslen(line) * 2), &written, nullptr);
    CloseHandle(h);
}

#ifdef __SANITIZE_ADDRESS__
// Build de diagnóstico ASan: sus informes van a stderr, que en una app GUI
// no lleva a ningún sitio. Se reabre el stderr como
// %AppData%\Acoustical\asan.log (sin buffer, para no perder nada cuando el
// proceso se aborta). Se llama al inicio de initialise(), antes de que
// pueda producirse el primer informe.
inline void redirectStderrToLog() {
    wchar_t base[MAX_PATH] = {};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base) != S_OK)
        return;
    wchar_t full[MAX_PATH] = {};
    swprintf(full, MAX_PATH, L"%s\\Acoustical\\asan.log", base);
    // 1) Nivel CRT: reabrir el stderr (cubre el caso de que el runtime de
    //    ASan escriba a traves del FILE* de stderr).
    char narrow[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, full, -1, narrow,
                        static_cast<int>(sizeof(narrow)) - 1, nullptr, nullptr);
    if (freopen(narrow, "ab", stderr) != nullptr)
        setvbuf(stderr, nullptr, _IONBF, 0);
    // 2) Nivel de proceso: por si el runtime escribe directamente a
    //    GetStdHandle(STD_ERROR_HANDLE) (el handle que ve el CRT no se
    //    actualiza con el SetStdHandle, y al reves; cubrimos ambos mundos).
    HANDLE h = CreateFileW(full, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
        SetStdHandle(STD_ERROR_HANDLE, h);
}
#endif  // __SANITIZE_ADDRESS__

// Volca hasta 256 B de memoria de nuestro propio proceso en hex
// ("48 89 …"). Devuelve el nº de bytes volcados (0 = no leible).
inline int dumpMem(unsigned long long addr, int n, wchar_t* out, int outChars) {
    unsigned char b[256];
    if (n > 256) n = 256;
    SIZE_T got = 0;
    if (!ReadProcessMemory(procHandle(), reinterpret_cast<LPCVOID>(addr), b,
                           static_cast<SIZE_T>(n), &got))
        return 0;
    int w = 0;
    for (SIZE_T i = 0; i < got && w < outChars - 8; ++i)
        w += swprintf(out + w, outChars - w, L"%s%02X", i ? L" " : L"", b[i]);
    return static_cast<int>(got);
}

inline LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
    const CONTEXT* ctx = ep->ContextRecord;

    // 1) Módulo (exe o DLL) que contiene la dirección del fallo. La
    //    enumeración se guarda: si no hay módulo que la contenga, se vuelca
    //    la lista completa al final.
    HMODULE mods[1024] = {};
    DWORD need = 0;
    const BOOL enumOk = EnumProcessModules(procHandle(), mods, sizeof(mods), &need) != 0;
    const int nMods = enumOk ? static_cast<int>(need / sizeof(HMODULE)) : 0;

    bool inModule = false;
    wchar_t modName[MAX_PATH] = L"(fuera de toda imagen cargada)";
    unsigned long long modOffset = 0;
    if (enumOk) {
        const auto addr = reinterpret_cast<unsigned long long>(rec->ExceptionAddress);
        for (int i = 0; i < nMods && mods[i] != nullptr; ++i) {
            MODULEINFO mi {};
            if (GetModuleInformation(procHandle(), mods[i], &mi, sizeof(mi)) == 0)
                continue;
            const auto base = reinterpret_cast<unsigned long long>(mi.lpBaseOfDll);
            if (addr >= base && addr < base + static_cast<unsigned long long>(mi.SizeOfImage)) {
                GetModuleBaseNameW(procHandle(), mods[i], modName, MAX_PATH);
                modOffset = addr - base;
                inModule = true;
                break;
            }
        }
    }

    // 2) 0xC0000005 (acceso a memoria): tipo de acceso y dirección afectada
    wchar_t av[160] = L"";
    if (rec->ExceptionCode == 0xC0000005 && rec->NumberParameters >= 2)
        swprintf(av, 80, L" (%s de 0x%llX)",
                 rec->ExceptionInformation[0] == 1 ? L"escritura" : L"lectura",
                 static_cast<unsigned long long>(rec->ExceptionInformation[1]));

    wchar_t line[1100] = {};
    swprintf(line, 1000,
             L"%04d-%02d-%02d %02d:%02d:%02d  CRASH: codigo de Windows 0x%08X%s en "
             L"%p = %s +0x%llX - la app se detuvo. Esta linea y las anteriores "
             L"dicen hasta donde llego el arranque\r\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             rec->ExceptionCode, av,
             rec->ExceptionAddress, modName, modOffset);
    append(line);

    // 3) Registros y estado de la enumeración de módulos
    {
        const auto rip = ctx
            ? static_cast<unsigned long long>(ctx->Rip)
            : reinterpret_cast<unsigned long long>(rec->ExceptionAddress);
        const auto rsp = ctx ? static_cast<unsigned long long>(ctx->Rsp) : 0ull;
        const auto rbp = ctx ? static_cast<unsigned long long>(ctx->Rbp) : 0ull;
        wchar_t dx[512] = {};
        if (enumOk)
            swprintf(dx, 500,
                     L"%04d-%02d-%02d %02d:%02d:%02d  CRASH-DX: RIP=0x%llX RSP=0x%llX "
                     L"RBP=0x%llX ENUM=ok (n=%d)",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     rip, rsp, rbp, nMods);
        else
            swprintf(dx, 500,
                     L"%04d-%02d-%02d %02d:%02d:%02d  CRASH-DX: RIP=0x%llX RSP=0x%llX "
                     L"RBP=0x%llX ENUM=fallo (err=%lu)",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     rip, rsp, rbp, static_cast<unsigned long>(GetLastError()));
        append(dx);
    }

    // 4) Bytes en la dirección del fallo: si es código real se ven
    //    instrucciones; si es memoria salvaje (puntero corrupto), ruido.
    {
        const auto rip = ctx
            ? static_cast<unsigned long long>(ctx->Rip)
            : reinterpret_cast<unsigned long long>(rec->ExceptionAddress);
        wchar_t buf[128] = {};
        const int got = dumpMem(rip, 16, buf, 128);
        wchar_t l[400] = {};
        swprintf(l, 390, L"%04d-%02d-%02d %02d:%02d:%02d  CRASH-BYTES-RIP: %s%s",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 got > 0 ? buf : L"", got > 0 ? L"" : L"(no leible)");
        append(l);
    }

    // 5) 128 B de pila alrededor de RSP: el frame dañado y el valor que
    //    sustituyó a la dirección de retorno, en su contexto.
    if (ctx) {
        const auto rsp = static_cast<unsigned long long>(ctx->Rsp);
        for (int part = 0; part < 2; ++part) {
            wchar_t buf[256] = {};
            const auto at = (part == 0) ? (rsp >= 32 ? rsp - 32 : rsp) : rsp + 32;
            const int got = dumpMem(at, 64, buf, 256);
            wchar_t l[420] = {};
            swprintf(l, 410,
                     L"%04d-%02d-%02d %02d:%02d:%02d  CRASH-STACK %d/2 (0x%llX): %s%s",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     part + 1, static_cast<unsigned long long>(at),
                     got > 0 ? buf : L"", got > 0 ? L"" : L"(no leible)");
            append(l);
        }
    }

    // 6) Si la dirección no caía en ninguna imagen cargada: lista de
    //    módulos (nombre base+tamaño). Sirve para (a) verificar que la
    //    enumeración funcionó de verdad y (b) ver la base del exe y
    //    comprobar si el fallo caía DENTRO de la app (offset = dirección -
    //    base → symbolizar con el PDB).
    if (enumOk && !inModule) {
        const int PER = 24;
        for (int part = 0; part < 2; ++part) {
            wchar_t l[3000] = {};
            int w = swprintf(l, 2900,
                             L"%04d-%02d-%02d %02d:%02d:%02d  CRASH-MODULOS %d/2: ",
                             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                             part + 1);
            const int from = part * PER, to = from + PER;
            for (int i = from; i < to && i < nMods && mods[i] != nullptr && w < 2800; ++i) {
                MODULEINFO mi {};
                if (GetModuleInformation(procHandle(), mods[i], &mi, sizeof(mi)) == 0)
                    continue;
                wchar_t nm[128] = L"?";
                GetModuleBaseNameW(procHandle(), mods[i], nm, 128);
                w += swprintf(l + w, 2900 - w, L"%s 0x%llX+0x%llX;", nm,
                              reinterpret_cast<unsigned long long>(mi.lpBaseOfDll),
                              static_cast<unsigned long long>(mi.SizeOfImage));
            }
            append(l);
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;   // → diálogo estándar de Windows
}

}  // namespace crashfilter
#endif  // JUCE_WINDOWS
