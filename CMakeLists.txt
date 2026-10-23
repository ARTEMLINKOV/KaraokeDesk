
cmake_minimum_required(VERSION 3.22)

# This is a macOS-only app. Set Apple toolchain defaults before project() so
# CMake uses them when it first enables the C/C++ compilers.
# Users can override these cache values for a different target configuration.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "macOS architectures")
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
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
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "KaraokeDesk использует аудиовходы для мониторинга микрофонов и сведения вокала в основной выход."
)

juce_generate_juce_header(KaraokeDesk)

target_sources(KaraokeDesk PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/KaraokeAudioEngine.h
    Source/KaraokeAudioEngine.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(KaraokeDesk PRIVATE
        Source/MacWindowBehavior.mm
        Source/SystemAudioCapture.mm
    )
    set_target_properties(KaraokeDesk PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
    )
endif()

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
        "-framework CoreMedia"
        "-framework AVFAudio"
        "-framework ScreenCaptureKit"
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
