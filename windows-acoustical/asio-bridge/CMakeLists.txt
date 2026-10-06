# Driver ASIO virtual en modo usuario (COM DLL). Se compila dos veces: x64 y
# Win32 (Cubase 5 de 32 y 64 bits). El script tools/build_all.ps1 lo hace solo.
cmake_minimum_required(VERSION 3.22)
project(AcousticalBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AcousticalBridge MODULE
    AcousticalBridge.cpp
)

set_target_properties(AcousticalBridge PROPERTIES
    OUTPUT_NAME "AcousticalBridge"
    PREFIX ""
)

target_compile_definitions(AcousticalBridge PRIVATE
    UNICODE _UNICODE
    NOMINMAX
)

target_link_libraries(AcousticalBridge PRIVATE
    ole32
    user32
    winmm
    advapi32
)

set_source_files_properties(AcousticalBridge.cpp PROPERTIES
    COMPILE_OPTIONS "/EHsc"
)

if(MSVC)
    # /guard:cf (Control Flow Guard): el driver corre DENTRO del proceso del
    # DAW (Cubase): un puntero a función corrupto pasa a ser una excepción
    # controlada (0x40000005) en vez de un salto al azar que tumbaría el host.
    target_compile_options(AcousticalBridge PRIVATE /guard:cf)
    target_link_options(AcousticalBridge PRIVATE /guard:cf)

    # PDB en Debug y Release (/Zi al compilar + /DEBUG al enlazar), la misma
    # política que la app, que genera sus PDBs a propósito: hoy el driver
    # no producía PDB y un crash dentro de Cubase con un offset del
    # AcousticalBridge.dll era irresoluble.
    target_compile_options(AcousticalBridge PRIVATE /Zi)
    target_link_options(AcousticalBridge PRIVATE /DEBUG)
endif()
