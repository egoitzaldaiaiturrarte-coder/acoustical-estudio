cmake_minimum_required(VERSION 3.22)
project(AcousticalEstudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Plugin VST3 + VST2.4 para Cubase 5
add_subdirectory(plugin)

# App de escritorio
add_subdirectory(app)
