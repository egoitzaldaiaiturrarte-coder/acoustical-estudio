# Núcleo DSP compartido (C puro) — la única implementación de FFT/agregación/
# biquad, usada tanto por la app Windows como por el móvil (JNI).
add_library(acoustical_dsp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/acoustical_dsp.c
)
target_include_directories(acoustical_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp)
target_compile_definitions(acoustical_dsp PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
# Se enlaza dentro del .so compartido del VST3 (y del APK vía JNI): -fPIC obligatorio.
set_target_properties(acoustical_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(acoustical_engine STATIC
    FftProcessor.cpp
    SweeperProcessor.cpp
    EqDsp.cpp
    AcousticalEngine.cpp
    SignalGenerator.cpp
)

target_link_libraries(acoustical_engine PUBLIC acoustical_dsp)
target_include_directories(acoustical_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(acoustical_engine PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
set_target_properties(acoustical_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
