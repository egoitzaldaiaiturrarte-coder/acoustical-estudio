# Driver ASIO virtual en modo usuario (COM DLL). Se compila dos veces: x64 y
# Win32 (Cubase 5 de 32 y 64 bits). El script tools/build_all.ps1 lo hace solo.
cmake_minimum_required(VERSION 3.22)
project(AcousticalBridge LANGUAGES CXX)

add_library(AcousticalBridge MODULE
    AcousticalBridge.cpp
)

set_target_properties(AcousticalBridge PROPERTIES
    OUTPUT_NAME "AcousticalBridge"
    PREFIX ""
)

target_compile_definitions(AcousticalBridge PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
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
