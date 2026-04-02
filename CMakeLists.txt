cmake_minimum_required(VERSION 3.22)

project(KlangKunstGalaxy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "C:/Users/matda/Documents/Playground/SAMvoice/JUCE" CACHE PATH "Path to JUCE")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_gui_app(KlangKunstGalaxy
    PRODUCT_NAME "KlangKunstGalaxy")

juce_generate_juce_header(KlangKunstGalaxy)

target_sources(KlangKunstGalaxy
    PRIVATE
        Source/Main.cpp
        Source/GameComponent.cpp
        Source/GameModel.cpp)

target_compile_definitions(KlangKunstGalaxy
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(KlangKunstGalaxy
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
