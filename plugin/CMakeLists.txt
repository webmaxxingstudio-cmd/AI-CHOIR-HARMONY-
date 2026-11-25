juce_add_plugin(VocalChoirStudio
    COMPANY_NAME "Voix Chorale"
    BUNDLE_ID "com.voixchorale.vocalchoirstudio"
    PLUGIN_MANUFACTURER_CODE Vxch
    PLUGIN_CODE Vcs1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Vocal Choir Studio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Shift
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VocalChoirStudio PRIVATE
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/DesignSystem.cpp
    Source/ui/Widgets.cpp)

target_compile_definitions(VocalChoirStudio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)

target_link_libraries(VocalChoirStudio
    PRIVATE
        vcs_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Host-side validation: loads the built VST3 the way a DAW does and exercises it.
option(VCS_BUILD_HOST_CHECK "Build the VST3 host validation tool" ON)
if(VCS_BUILD_HOST_CHECK)
    juce_add_console_app(vcs_host_check PRODUCT_NAME "vcs_host_check")
    target_sources(vcs_host_check PRIVATE ${CMAKE_SOURCE_DIR}/tools/HostCheck.cpp)
    target_include_directories(vcs_host_check PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(vcs_host_check PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(vcs_host_check PRIVATE
        vcs_core juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        juce::juce_recommended_config_flags)
    add_dependencies(vcs_host_check VocalChoirStudio_VST3)
endif()

# UI snapshot tool: links the plugin's shared code and renders the editor to PNG.
if(VCS_BUILD_HOST_CHECK)
    add_executable(vcs_snapshot ${CMAKE_SOURCE_DIR}/tools/Snapshot.cpp)
    target_include_directories(vcs_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/tests
        $<TARGET_PROPERTY:VocalChoirStudio,INCLUDE_DIRECTORIES>)
    target_compile_definitions(vcs_snapshot PRIVATE $<TARGET_PROPERTY:VocalChoirStudio,COMPILE_DEFINITIONS>)
    target_link_libraries(vcs_snapshot PRIVATE VocalChoirStudio)
endif()
