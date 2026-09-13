# Plugin VST para Cubase 5 — VST3 siempre; VST2.4 (32-bit) for Cubase 5.
# The legacy VST2 SDK is vendored in windows-acoustical/vst2-sdk. Override with
# the VST2_SDK_PATH env var if you have a different/complete SDK.
set(_vst2_sdk "$ENV{VST2_SDK_PATH}")
if(NOT _vst2_sdk)
    set(_vst2_sdk "${CMAKE_CURRENT_SOURCE_DIR}/../vst2-sdk")
endif()
if(EXISTS "${_vst2_sdk}/pluginterfaces/vst2.x/aeffect.h")
    juce_set_vst2_sdk_path("${_vst2_sdk}")
    set(FORMATS VST3 VST)
    message(STATUS "Acoustical: compilando VST3 + VST2.4 (SDK: ${_vst2_sdk})")
else()
    message(STATUS "Acoustical: VST2 SDK no encontrado — solo VST3")
    set(FORMATS VST3)
endif()

juce_add_plugin(AcousticalDynamicEq
    PLUGIN_MANUFACTURER_CODE Acos
    PLUGIN_CODE Aeq1
    FORMATS ${FORMATS}
    PRODUCT_NAME "Acoustical Dynamic EQ"
    COMPANY_NAME "Acoustical"
    BUNDLE_ID "com.acoustical.dynamiceq"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx
)

target_sources(AcousticalDynamicEq PRIVATE
    PluginProcessor.cpp
    PluginEditor.h
)

target_compile_definitions(AcousticalDynamicEq PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if(MSVC)
    target_compile_options(AcousticalDynamicEq PRIVATE /utf-8)
endif()

# Reutiliza el canvas de EQ y las tarjetas de los ecuas dinámicos de la app
target_include_directories(AcousticalDynamicEq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app)

target_link_libraries(AcousticalDynamicEq PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics
    acoustical_engine
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
