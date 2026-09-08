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
