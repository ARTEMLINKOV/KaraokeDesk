cmake_minimum_required(VERSION 3.22)
project(KaraokeDesk VERSION 1.0.0 LANGUAGES C CXX)

# Install JUCE (https://juce.com/download/) and pass -DJUCE_DIR=/path/to/JUCE
if(NOT DEFINED JUCE_DIR)
  message(FATAL_ERROR "Set JUCE_DIR to your JUCE source folder, e.g. -DJUCE_DIR=$HOME/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_gui_app(KaraokeDesk
    PRODUCT_NAME "KaraokeDesk"
    COMPANY_NAME "KaraokeDesk"
    BUNDLE_ID "com.karaokedesk.app"
)

juce_generate_juce_header(KaraokeDesk)

target_sources(KaraokeDesk PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/KaraokeAudioEngine.h
    Source/KaraokeAudioEngine.cpp
)

target_compile_features(KaraokeDesk PRIVATE cxx_std_17)
target_compile_definitions(KaraokeDesk PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)
target_link_libraries(KaraokeDesk PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
