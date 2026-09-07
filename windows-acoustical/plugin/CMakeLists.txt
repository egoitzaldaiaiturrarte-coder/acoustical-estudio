# Plugin VST para Cubase 5 — VST3 siempre; VST2.4 si está el SDK legacy
# (variable de entorno VST2_SDK_PATH apuntando a la carpeta con aeffect.h).
set(FORMATS VST3)
if(DEFINED ENV{VST2_SDK_PATH})
    juce_set_vst2_sdk_path("$ENV{VST2_SDK_PATH}")
    set(FORMATS VST3 VST2)
    message(STATUS "Acoustical: compilando VST3 + VST2.4")
else()
    message(STATUS "Acoustical: VST2_SDK_PATH no definido — solo VST3")
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
