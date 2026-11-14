cmake_minimum_required(VERSION 3.22)

# Set the deployment target before project() initialises the Apple toolchain.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
        "Minimum supported macOS version")
endif()

project(GainShareAutomixer
    VERSION 1.0.0
    LANGUAGES C CXX)

add_subdirectory(JUCE)

juce_add_plugin(GainShareAutomixer
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Reid Dethorne"
    COMPANY_WEBSITE "https://github.com/reiddethorne"
    BUNDLE_ID "io.github.reiddethorne.gainshareautomixer"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Wagu
    DESCRIPTION "Four-input gain-sharing automixer"
    PRODUCT_NAME "GainShare Automixer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT
        "Automixer needs microphone access to process live audio."
    FORMATS Standalone)

target_sources(GainShareAutomixer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)

target_compile_features(GainShareAutomixer PRIVATE cxx_std_17)

# The existing sources include <JuceHeader.h>. Generate it in the build tree
# so generated JUCE files never need to be stored in the source tree.
juce_generate_juce_header(GainShareAutomixer)

target_compile_definitions(GainShareAutomixer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(GainShareAutomixer
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

include(CTest)

# Timing is opt-in and is deliberately separate from pass/fail tests and CI.
add_executable(GainShareAutomixerDspBenchmark EXCLUDE_FROM_ALL Benchmarks/DspBenchmark.cpp)
target_compile_features(GainShareAutomixerDspBenchmark PRIVATE cxx_std_17)
target_include_directories(GainShareAutomixerDspBenchmark PRIVATE Source
    $<TARGET_PROPERTY:GainShareAutomixer,JUCE_GENERATED_SOURCES_DIRECTORY>)
target_link_libraries(GainShareAutomixerDspBenchmark PRIVATE
    GainShareAutomixer juce::juce_audio_utils)
set_target_properties(GainShareAutomixerDspBenchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks/$<CONFIG>")

if(BUILD_TESTING)
    add_executable(GainShareAutomixerDspTests Tests/DspTests.cpp)
    target_compile_features(GainShareAutomixerDspTests PRIVATE cxx_std_17)
    target_include_directories(GainShareAutomixerDspTests PRIVATE Source
        $<TARGET_PROPERTY:GainShareAutomixer,JUCE_GENERATED_SOURCES_DIRECTORY>)
    target_link_libraries(GainShareAutomixerDspTests PRIVATE
        GainShareAutomixer juce::juce_audio_utils)

    foreach(sample_rate IN ITEMS 44100 48000 96000)
        foreach(block_size IN ITEMS 32 128 512)
            foreach(test_case IN ITEMS silence equal_inputs single_input muted_input bypass output_trim stereo)
                add_test(NAME dsp.${test_case}.${sample_rate}.${block_size}
                    COMMAND GainShareAutomixerDspTests ${test_case} ${sample_rate} ${block_size})
            endforeach()
        endforeach()
    endforeach()
    add_test(NAME dsp.state_recall
        COMMAND GainShareAutomixerDspTests state_recall 48000 128)
endif()
