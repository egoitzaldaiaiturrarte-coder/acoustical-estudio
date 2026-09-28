// Syntax-check de CrashFilter.h (camino solo-Windows) contra las firmas
// REALES de las API de Win32 (tools/fakewin), en Linux. Si algo no compila
// aquí, no compila contra el SDK de MSVC.
//
// Uso: g++ -std=c++17 -fsyntax-only -DJUCE_WINDOWS=1 -Itools/fakewin -Iapp tools/test_crashfilter.cpp
#define JUCE_WINDOWS 1
#include "CrashFilter.h"

int main() {
    // Referenciar ambas funciones para que el compilador las valide por
    // completo (cuerpo + firmas + formatos swprintf).
    (void)crashfilter::append;
    (void)crashfilter::filter;
    return 0;
}
