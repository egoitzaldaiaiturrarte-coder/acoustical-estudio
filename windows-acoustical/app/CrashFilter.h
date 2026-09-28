// CrashFilter.h — filtro de excepciones fatal (solo Windows/MSVC), C puro.
//
// Si la app muere por un error fatal (p. ej. 0xC0000005 = acceso a memoria),
// Windows muestra su diálogo y el proceso muere. Para poder ver DÓNDE murió,
// anotamos en %AppData%\Acoustical\startup.log:
//   · el código de excepción y la dirección del fallo
//   · el MÓDULO (exe o DLL) que contiene esa dirección, con su nombre y el
//     desplazamiento desde su base (¿es nuestra app o un driver/librería?)
//   · en un 0xC0000005, si era lectura o escritura y la dirección afectada
//
// Todo con Win32 puro (sin JUCE ni std): a esa altura no se puede fiar de nada.
// El archivo se valida en CI contra estas firmas reales (tools/fakewin/).
#pragma once

#ifdef JUCE_WINDOWS
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <cwchar>

namespace crashfilter {

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

inline LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const EXCEPTION_RECORD* rec = ep->ExceptionRecord;

    // Módulo (exe o DLL) que contiene la dirección del fallo: el nombre y el
    // desplazamiento desde su base dicen si el crash es en nuestro código o
    // en un driver/librería (NVIDIA, WASAPI, COM…).
    wchar_t modName[MAX_PATH] = L"(fuera de toda imagen cargada)";
    unsigned long long modOffset = 0;
    {
        HMODULE mods[1024] = {};
        DWORD need = 0;
        if (EnumProcessModules(GetCurrentProcess, mods, sizeof(mods), &need) != 0) {
            const int n = static_cast<int>(need / sizeof(HMODULE));
            const auto addr = reinterpret_cast<unsigned long long>(rec->ExceptionAddress);
            for (int i = 0; i < n; ++i) {
                MODULEINFO mi {};
                if (GetModuleInformation(GetCurrentProcess, mods[i], &mi, sizeof(mi)) == 0)
                    continue;
                const auto base = reinterpret_cast<unsigned long long>(mi.lpBaseOfDll);
                if (addr >= base && addr < base + static_cast<unsigned long long>(mi.SizeOfImage)) {
                    GetModuleBaseNameW(GetCurrentProcess, mods[i], modName, MAX_PATH);
                    modOffset = addr - base;
                    break;
                }
            }
        }
    }

    // 0xC0000005 (acceso a memoria): tipo de acceso y la dirección que falló
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
    return EXCEPTION_EXECUTE_HANDLER;   // → diálogo estándar de Windows
}

}  // namespace crashfilter
#endif  // JUCE_WINDOWS
