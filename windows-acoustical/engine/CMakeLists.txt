add_library(acoustical_engine STATIC
    FftProcessor.cpp
    SweeperProcessor.cpp
    EqDsp.cpp
    AcousticalEngine.cpp
    SignalGenerator.cpp
)

target_include_directories(acoustical_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(acoustical_engine PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
