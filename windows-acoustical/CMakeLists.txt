cmake_minimum_required(VERSION 3.22)
project(AcousticalEstudio VERSION 1.6.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the plugin DLL has no VC++ redistributable dependency
# (Cubase 5 era machines often lack the VC++ 2015+ runtime).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Build de diagnóstico con AddressSanitizer (MSVC): -DACOUSTICAL_ASAN=ON.
# Lo usa el job "asan" del workflow (binario para cazar la primera
# violación de heap con la pila del hilo culpable).
option(ACOUSTICAL_ASAN "Build with AddressSanitizer (diagnostic only)" OFF)

# JUCE se descarga automáticamente la primera vez (necesita conexión a internet)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Motor compartido (app + plugin). Puro C++17, sin dependencias.
add_subdirectory(engine)

# Plugin VST3 + VST2.4 (x64 para Cubase 15 y Win32 para Cubase 5 de 32 bits)
add_subdirectory(plugin)

# App de escritorio
add_subdirectory(app)
