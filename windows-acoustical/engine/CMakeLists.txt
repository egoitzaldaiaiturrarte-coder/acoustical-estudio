# Núcleo DSP compartido (C puro) — la única implementación de FFT/agregación/
# biquad, usada tanto por la app Windows como por el móvil (JNI).
add_library(acoustical_dsp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp/acoustical_dsp.c
)
target_include_directories(acoustical_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../dsp)
# Sin defines JUCE: este es un target de C puro que no incluye ningún header
# de JUCE. Los defines JUCE_WEB_BROWSER/JUCE_USE_CURL solo importan para el
# código que compila los MÓDULOS de JUCE (los targets de la app y el plugin,
# que se los fijan por su cuenta) — aquí era ruido (y en C puro, ni eso).
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
# Mismo razonamiento que en acoustical_dsp: el motor no incluye JUCE, los
# defines de módulos solo importan en los targets consumidores.
set_target_properties(acoustical_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# LTO igual que en la app y el plugin: compila el motor (el código más
# sensible del proyecto) con /GL en Release y enlaza con -LTCG. Solo en
# builds normales: con ASan el LTO puede degradar los informes (marcos
# optimizados de forma agresiva en los límites de módulo), misma política
# que la app (ver el bloque ACOUSTICAL_ASAN de app/CMakeLists.txt).
if(NOT ACOUSTICAL_ASAN)
    target_link_libraries(acoustical_dsp PUBLIC juce::juce_recommended_lto_flags)
    target_link_libraries(acoustical_engine PUBLIC juce::juce_recommended_lto_flags)
endif()

# PDB en builds MSVC (el build ASan y el filtro de crash simbolizan con él).
if(MSVC)
    # /guard:cf (Control Flow Guard): los indirect calls del motor llevan
    # comprobación de integridad en runtime; si se corrompe una puntero a
    # función, el fallo es una excepción controlada y no un salto al azar.
    target_compile_options(acoustical_dsp PRIVATE /guard:cf)
    target_compile_options(acoustical_engine PRIVATE /guard:cf)
    target_compile_options(acoustical_dsp PRIVATE /Zi)
    target_compile_options(acoustical_engine PRIVATE /Zi)
endif()
