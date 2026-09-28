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
    JUCE_APPLICATION_VERSION_STRING="1.6.4"
)

if(MSVC)
    target_compile_options(AcousticalEstudio PRIVATE /utf-8)
    # psapi: el filtro de crash (Main.cpp) usa EnumProcessModules/
    # GetModuleInformation/GetModuleBaseNameW para decir en qué módulo
    # (exe o DLL) ha fallado la dirección del fault.
    target_link_libraries(AcousticalEstudio PRIVATE psapi)
endif()

# Build de diagnóstico con AddressSanitizer (opción global, ver el job "asan"
# del workflow): instrumenta la app, el motor y los módulos de JUCE para
# capturar la PRIMERA violación de heap del proceso (nuestra o de una DLL)
# con la pila exacta del hilo culpable.
if(ACOUSTICAL_ASAN AND MSVC)
    target_compile_options(AcousticalEstudio PRIVATE /O1 /fsanitize=address)
    target_link_options(AcousticalEstudio PRIVATE /fsanitize=address)
    foreach(_asan_module juce_core juce_data_structures juce_events juce_graphics
                         juce_gui_basics juce_gui_extra juce_audio_basics
                         juce_audio_devices juce_audio_formats juce_audio_utils
                         juce_cryptography juce_dsp juce_midi_basics juce_video)
        if(TARGET ${_asan_module})
            target_compile_options(${_asan_module} PRIVATE /O1 /fsanitize=address)
        endif()
    endforeach()
    target_compile_options(acoustical_engine PRIVATE /O1 /fsanitize=address)
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
