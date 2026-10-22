
cmake_minimum_required(VERSION 3.22)

# Keep Intel Mac compatibility by default. Users can override these cache values
# when intentionally building for another architecture or deployment target.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "macOS architectures")
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(KaraokeDesk
    VERSION 0.1.0
    LANGUAGES C CXX
)

# Путь к исходникам JUCE задаётся через -DJUCE_DIR=...
if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR
        "Set JUCE_DIR to your JUCE source folder, e.g. -DJUCE_DIR=$HOME/JUCE"
    )
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE_DIR does not point to a valid JUCE source directory: ${JUCE_DIR}"
    )
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

target_compile_features(KaraokeDesk PRIVATE
    cxx_std_17
)

target_compile_definitions(KaraokeDesk PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

if(APPLE)
    target_compile_definitions(KaraokeDesk PRIVATE JUCE_PLUGINHOST_AU=1)
    target_link_libraries(KaraokeDesk PRIVATE
        "-framework AudioUnit"
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
endif()

target_link_libraries(KaraokeDesk PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
