juce_add_gui_app(AcousticalEstudio
    PRODUCT_NAME "Acoustical Estudio"
    COMPANY_NAME "Acoustical"
    BUNDLE_ID "com.acoustical.estudio")

target_sources(AcousticalEstudio PRIVATE
    Main.cpp
    PhoneLink.cpp
)

target_compile_definitions(AcousticalEstudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="Acoustical Estudio"
    JUCE_APPLICATION_VERSION_STRING="1.0.0"
)

target_link_libraries(AcousticalEstudio PRIVATE
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_cryptography
    acoustical_engine
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
