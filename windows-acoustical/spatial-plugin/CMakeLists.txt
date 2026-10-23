# "Acoustical Spatial" (v1.11.0) — el VST COMPLEMENTARIO del Acoustical
# Dynamic EQ: la posición espacial del elemento (joystick x/y + faders de
# distancia/tamaño + entorno) en un panel grande y cómodo, en vez de los
# cuatro sliders en fila del editor del VST principal. El audio que pasa por
# él recibe la MISMA compensación espacial (ley del core C).
#
# Mismo patrón de build que el VST principal: VST3 x64 (Cubase 15 y DAWs de
# 64 bits) + VST3/VST2.4 Win32 si hay SDK (Cubase 5 de 32 bits).
set(_vst2_sdk "$ENV{VST2_SDK_PATH}")
if(NOT _vst2_sdk)
    set(_vst2_sdk "${CMAKE_CURRENT_SOURCE_DIR}/../vst2-sdk")
endif()
if(EXISTS "${_vst2_sdk}/pluginterfaces/vst2.x/aeffect.h")
    juce_set_vst2_sdk_path("${_vst2_sdk}")
    set(FORMATS VST3 VST)
    message(STATUS "Acoustical Spatial: compilando VST3 + VST2.4 (SDK: ${_vst2_sdk})")
else()
    message(STATUS "Acoustical Spatial: VST2 SDK no encontrado — solo VST3")
    set(FORMATS VST3)
endif()

juce_add_plugin(AcousticalSpatial
    PLUGIN_MANUFACTURER_CODE Acos
    PLUGIN_CODE Asp1
    VERSION 1.11.0
    FORMATS ${FORMATS}
    PRODUCT_NAME "Acoustical Spatial"
    COMPANY_NAME "Acoustical"
    BUNDLE_ID "com.acoustical.spatial"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx
)

target_sources(AcousticalSpatial PRIVATE
    SpatialProcessor.cpp
    SpatialEditor.h
)

target_compile_definitions(AcousticalSpatial PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if(MSVC)
    # /utf-8: los fuentes del plugin llevan acentos/UTF-8.
    # /guard:cf: el plugin corre DENTRO del DAW (misma política que el
    # VST principal: un puntero corrupto produce excepción controlada, no
    # un salto al azar que tumbaría el host).
    target_compile_options(AcousticalSpatial PRIVATE /utf-8 /guard:cf)
    target_link_options(AcousticalSpatial PRIVATE /guard:cf)
    if(TARGET AcousticalSpatial_VST3)
        target_compile_options(AcousticalSpatial_VST3 PRIVATE /guard:cf)
        target_link_options(AcousticalSpatial_VST3 PRIVATE /guard:cf)
    endif()
    if(TARGET AcousticalSpatial_VST)
        target_compile_options(AcousticalSpatial_VST PRIVATE /guard:cf)
        target_link_options(AcousticalSpatial_VST PRIVATE /guard:cf)
    endif()
endif()

# Reutiliza los componentes de la app (paleta + joystick espacial)
target_include_directories(AcousticalSpatial PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app)

target_link_libraries(AcousticalSpatial PRIVATE
    juce::juce_gui_basics
    acoustical_dsp
    PUBLIC
    juce::juce_recommended_config_flags
)

# LTO: misma política que el VST principal (solo builds normales).
if(NOT ACOUSTICAL_ASAN)
    target_link_libraries(AcousticalSpatial PUBLIC juce::juce_recommended_lto_flags)
endif()

# Build de diagnóstico con AddressSanitizer (misma cobertura que el VST
# principal: código compartido instrumentado + wrappers VST3/VST2).
if(ACOUSTICAL_ASAN AND MSVC)
    target_compile_options(AcousticalSpatial PRIVATE /fsanitize=address)
    if(TARGET AcousticalSpatial_VST3)
        target_compile_options(AcousticalSpatial_VST3 PRIVATE /fsanitize=address)
        target_link_options(AcousticalSpatial_VST3 PRIVATE /fsanitize=address)
    endif()
    if(TARGET AcousticalSpatial_VST)
        target_compile_options(AcousticalSpatial_VST PRIVATE /fsanitize=address)
        target_link_options(AcousticalSpatial_VST PRIVATE /fsanitize=address)
    endif()
endif()
