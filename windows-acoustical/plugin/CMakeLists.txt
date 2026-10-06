# Plugin VST — VST3 x64 (Cubase 15 y DAWs de 64 bits) + VST3/VST2.4 Win32
# (Cubase 5 de 32 bits, que solo carga plugins de 32 bits). El VST3 es la via
# recomendada en Cubase 15; el VST2 Win32 queda para la cubeta 32-bit.
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
    # /utf-8: los fuentes del plugin llevan acentos/UTF-8.
    # /guard:cf (Control Flow Guard): el plugin corre DENTRO del DAW; un
    # puntero a función corrupto produce una excepción controlada
    # (0x40000005) en vez de un salto al azar que tumbaría el host.
    target_compile_options(AcousticalDynamicEq PRIVATE /utf-8 /guard:cf)
    # El target "shared code" es una lib estática: sus opciones de enlace no
    # se usan; los flags de enlace van en los wrappers (abajo), que son los
    # que producen los dll finales.
    target_link_options(AcousticalDynamicEq PRIVATE /guard:cf)
    # Wrappers VST3/VST2: los dll que enlaza el DAW también se compilan y
    # enlazan con CFG (solo se generan los targets que hay, según FORMATS).
    if(TARGET AcousticalDynamicEq_VST3)
        target_compile_options(AcousticalDynamicEq_VST3 PRIVATE /guard:cf)
        target_link_options(AcousticalDynamicEq_VST3 PRIVATE /guard:cf)
    endif()
    if(TARGET AcousticalDynamicEq_VST)
        target_compile_options(AcousticalDynamicEq_VST PRIVATE /guard:cf)
        target_link_options(AcousticalDynamicEq_VST PRIVATE /guard:cf)
    endif()
endif()

# Reutiliza el canvas de EQ y las tarjetas de los ecuas dinámicos de la app
target_include_directories(AcousticalDynamicEq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app)

target_link_libraries(AcousticalDynamicEq PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics
    acoustical_engine
    PUBLIC
    juce::juce_recommended_config_flags
)

# LTO: solo en builds normales; con ASan puede degradar los informes
# (marcos optimizados de forma agresiva en los límites de módulo). Misma
# política que la app (app/CMakeLists.txt).
if(NOT ACOUSTICAL_ASAN)
    target_link_libraries(AcousticalDynamicEq PUBLIC juce::juce_recommended_lto_flags)
endif()

# Build de diagnóstico con AddressSanitizer (opción global, ver el job "asan"
# del workflow): el plugin se instrumenta igual que la app. En JUCE 7 los
# targets de módulo son INTERFACE (sus .cpp se compilan como parte del target
# consumidor), así que instrumentar el target de código compartido cubre la
# app del plugin Y los módulos de JUCE que usa; los wrappers VST3/VST2 (los
# dll que enlaza el DAW) se instrumentan y enlazan con el runtime ASan ellos
# mismos, para que también su glue de host esté cubierto.
if(ACOUSTICAL_ASAN AND MSVC)
    target_compile_options(AcousticalDynamicEq PRIVATE /fsanitize=address)
    if(TARGET AcousticalDynamicEq_VST3)
        target_compile_options(AcousticalDynamicEq_VST3 PRIVATE /fsanitize=address)
        target_link_options(AcousticalDynamicEq_VST3 PRIVATE /fsanitize=address)
    endif()
    if(TARGET AcousticalDynamicEq_VST)
        target_compile_options(AcousticalDynamicEq_VST PRIVATE /fsanitize=address)
        target_link_options(AcousticalDynamicEq_VST PRIVATE /fsanitize=address)
    endif()
endif()
