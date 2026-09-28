juce_add_gui_app(AcousticalEstudio
    PRODUCT_NAME "Acoustical Estudio"
    COMPANY_NAME "Acoustical"
    BUNDLE_ID "com.acoustical.estudio")

target_sources(AcousticalEstudio PRIVATE
    Main.cpp
    PhoneLink.cpp
    SyncClient.cpp
    BeaconListener.cpp
    RemoteAudioLink.cpp
)

target_compile_definitions(AcousticalEstudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    NOMINMAX
    JUCE_APPLICATION_NAME_STRING="Acoustical Estudio"
    JUCE_APPLICATION_VERSION_STRING="1.6.9"
)

if(MSVC)
    target_compile_options(AcousticalEstudio PRIVATE /utf-8 /Zi)
    # /Zi + /DEBUG: genera el PDB incluso en Release. Lo usa el filtro de
    # crash (mapear "exe +0x..." a función/línea) y el build ASan (los
    # informes simbolizan los marcos con el PDB junto al exe).
    target_link_options(AcousticalEstudio PRIVATE /DEBUG)
    # psapi: el filtro de crash (Main.cpp) usa EnumProcessModules/
    # GetModuleInformation/GetModuleBaseNameW para decir en qué módulo
    # (exe o DLL) ha fallado la dirección del fault.
    target_link_libraries(AcousticalEstudio PRIVATE psapi)
endif()

# Build de diagnóstico con AddressSanitizer (opción global, ver el job "asan"
# del workflow): instrumenta la app, el motor y el núcleo DSP para capturar la
# PRIMERA violación de heap del proceso (nuestra o de una DLL) con la pila
# exacta del hilo culpable.
#
# Nota: en JUCE 7 los targets de módulo son INTERFACE (sus .cpp se compilan
# como parte del target consumidor), así que el flag sobre AcousticalEstudio
# cubre la app Y todo el código de JUCE. El /O2 de
# juce_recommended_config_flags es compatible con ASan (docs MSVC).
if(ACOUSTICAL_ASAN AND MSVC)
    target_compile_options(AcousticalEstudio PRIVATE /fsanitize=address)
    target_link_options(AcousticalEstudio PRIVATE /fsanitize=address)
    target_compile_options(acoustical_engine PRIVATE /fsanitize=address)
    target_compile_options(acoustical_dsp PRIVATE /fsanitize=address)
endif()

# Protocolo de memoria compartida del puente ASIO (AsioBridgeClient.h)
target_include_directories(AcousticalEstudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../asio-bridge)

target_link_libraries(AcousticalEstudio PRIVATE
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_cryptography
    acoustical_engine
    PUBLIC
    juce::juce_recommended_config_flags
)

# LTO: solo en builds normales; con ASan puede degradar los informes
# (marcos optimizados de forma agresiva en los límites de módulo).
if(NOT ACOUSTICAL_ASAN)
    target_link_libraries(AcousticalEstudio PRIVATE juce::juce_recommended_lto_flags)
endif()
