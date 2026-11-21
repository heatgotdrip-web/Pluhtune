cmake_minimum_required(VERSION 3.22)
project(PLUHTUNE VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PLUHTUNE
    COMPANY_NAME "PLUHTUNE Audio"
    PLUGIN_MANUFACTURER_CODE Plht
    PLUGIN_CODE Plt1
    FORMATS VST3 Standalone
    PRODUCT_NAME "PLUHTUNE"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PLUHTUNE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp)

target_include_directories(PLUHTUNE PRIVATE Source Source/DSP Source/UI)

target_compile_definitions(PLUHTUNE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PLUHTUNE PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Optional: DSP unit tests (no JUCE needed)
add_executable(pluhtune_dsp_test tests/dsp_test.cpp)
target_include_directories(pluhtune_dsp_test PRIVATE Source/DSP)
