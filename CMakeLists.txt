
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
    PLIST_TO_MERGE
        "<plist><dict><key>NSPrincipalClass</key><string>KaraokeDeskCEFApplication</string></dict></plist>"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "KaraokeDesk использует аудиовходы для мониторинга микрофонов и сведения вокала в основной выход."
)

juce_generate_juce_header(KaraokeDesk)

target_sources(KaraokeDesk PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/AppleMusicChromium.h
    Source/KaraokeAudioEngine.h
    Source/KaraokeAudioEngine.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(KaraokeDesk PRIVATE
        Source/MacWindowBehavior.mm
        Source/SystemAudioCapture.mm
        Source/AppleMusicChromium.mm
    )
    set_target_properties(KaraokeDesk PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
    )

    if(NOT DEFINED CEF_ROOT OR NOT EXISTS "${CEF_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "Set CEF_ROOT to the extracted CEF macOS x86_64 distribution."
        )
    endif()

    list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
    find_package(CEF REQUIRED)
    add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/cef/libcef_dll_wrapper")

    target_include_directories(KaraokeDesk PRIVATE "${CEF_INCLUDE_PATH}")
    target_compile_definitions(KaraokeDesk PRIVATE ${CEF_COMPILER_DEFINES})
    target_compile_features(KaraokeDesk PRIVATE cxx_std_20)
    target_link_libraries(KaraokeDesk PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})

    set(CEF_HELPER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cef_helpers/$<CONFIG>")
    foreach(_helper_spec IN LISTS CEF_HELPER_APP_SUFFIXES)
        string(REPLACE ":" ";" _helper_parts "${_helper_spec}")
        list(GET _helper_parts 0 _helper_name_suffix)
        list(GET _helper_parts 1 _helper_target_suffix)
        list(GET _helper_parts 2 _helper_bundle_suffix)

        set(_helper_target "KaraokeDeskHelper${_helper_target_suffix}")
        set(_helper_name "KaraokeDesk Helper${_helper_name_suffix}")
        set(_helper_plist "${CMAKE_CURRENT_BINARY_DIR}/cef-helper${_helper_target_suffix}-Info.plist")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Source/CefHelper-Info.plist.in" _helper_plist_contents)
        string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_name}" _helper_plist_contents "${_helper_plist_contents}")
        string(REPLACE "\${PRODUCT_NAME}" "${_helper_name}" _helper_plist_contents "${_helper_plist_contents}")
        string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_helper_bundle_suffix}" _helper_plist_contents "${_helper_plist_contents}")
        file(WRITE "${_helper_plist}" "${_helper_plist_contents}")

        add_executable(${_helper_target} MACOSX_BUNDLE Source/CefHelperMain.cpp)
        SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
        target_include_directories(${_helper_target} PRIVATE "${CEF_INCLUDE_PATH}")
        target_compile_definitions(${_helper_target} PRIVATE ${CEF_COMPILER_DEFINES})
        target_compile_features(${_helper_target} PRIVATE cxx_std_20)
        target_link_libraries(${_helper_target} PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
        set_target_properties(${_helper_target} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${_helper_plist}"
            OUTPUT_NAME "${_helper_name}"
            RUNTIME_OUTPUT_DIRECTORY "${CEF_HELPER_OUTPUT_DIR}"
        )
        add_dependencies(KaraokeDesk ${_helper_target})
        add_custom_command(TARGET KaraokeDesk POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CEF_HELPER_OUTPUT_DIR}/${_helper_name}.app"
                "$<TARGET_BUNDLE_DIR:KaraokeDesk>/Contents/Frameworks/${_helper_name}.app"
            VERBATIM
        )
    endforeach()

    COPY_MAC_FRAMEWORK(KaraokeDesk "${CEF_BINARY_DIR}" "$<TARGET_BUNDLE_DIR:KaraokeDesk>")
    add_custom_command(TARGET KaraokeDesk POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CEF_ROOT}/LICENSE.txt"
            "$<TARGET_BUNDLE_DIR:KaraokeDesk>/Contents/Resources/CEF-LICENSE.txt"
        VERBATIM
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
