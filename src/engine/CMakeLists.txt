add_library(emerick_engine
    src/audio_analysis.cpp
    src/audio_backend.cpp
    src/clap_plugin.cpp
    src/clap_library.cpp
    src/clap_params.cpp
    src/plugin_scan.cpp
    src/clip_stream.cpp
    src/effects.cpp
    src/engine.cpp
    src/interleaved_io.cpp
    src/libremidi_backend.cpp
    src/loudness_match.cpp
    src/metronome_sounds.cpp
    src/midi_ports.cpp
    src/mix_issues.cpp
    src/note_analysis.cpp
    src/plugin.cpp
    src/rt_clap.cpp
    src/rt_metronome.cpp
    src/rt_effects.cpp
    src/rt_limiter.cpp
    src/rt_midi.cpp
    src/rt_process.cpp
    src/rt_standard_effects.cpp
    src/rt_vst3.cpp
    src/snapshot_builder.cpp
    src/spectrum.cpp
    src/take_recorder.cpp
    src/virtual_midi_backend.cpp
    src/vst3_plugin.cpp
    src/vst3_scan.cpp
    src/window_meter.cpp)
if(APPLE)
    target_sources(emerick_engine PRIVATE
        src/core_audio_backend_apple.cpp src/device_list_listener_apple.cpp src/private_aggregate_apple.cpp)
    target_link_libraries(emerick_engine PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
else()
    target_sources(emerick_engine PRIVATE src/miniaudio_backend.cpp)
    target_link_libraries(emerick_engine PRIVATE miniaudio)
endif()
target_include_directories(emerick_engine PUBLIC include)
target_link_libraries(emerick_engine
    PUBLIC emerick_core
    PRIVATE libremidi readerwriterqueue PkgConfig::SNDFILE clap-helpers vst3_sdk ${CMAKE_DL_LIBS})
emerick_target(emerick_engine)

add_executable(emerick-plugin-scanner scanner/plugin_scanner.cpp)
target_link_libraries(emerick-plugin-scanner PRIVATE emerick_engine nlohmann_json::nlohmann_json)
# MinGW's libstdc++ keeps std::print's Windows console support in a separate library.
if(MINGW)
    target_link_libraries(emerick-plugin-scanner PRIVATE stdc++exp)
endif()
emerick_target(emerick-plugin-scanner)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_engine_tests tests/audio_analysis_test.cpp tests/automation_test.cpp tests/bus_test.cpp tests/clap_test.cpp tests/effects_test.cpp tests/engine_test.cpp tests/fade_test.cpp tests/gm_test.cpp tests/interleaved_io_test.cpp tests/latency_test.cpp tests/listed_devices_test.cpp
        tests/metronome_test.cpp tests/cycle_test.cpp tests/midi_chase_test.cpp tests/midi_route_test.cpp tests/note_analysis_test.cpp tests/preview_test.cpp
        tests/recording_test.cpp tests/spectrum_test.cpp tests/standard_effects_test.cpp tests/stream_health_test.cpp tests/take_progress_test.cpp tests/vst3_test.cpp tests/window_meter_test.cpp)
    target_include_directories(emerick_engine_tests PRIVATE src)

    # Test instruments as CLAP bundles: a sine synth, and copies whose entry crashes or hangs.
    set(test_clap_folder ${CMAKE_BINARY_DIR}/test-clap)
    foreach(variant sine crash hang)
        set(name emerick_test_${variant})
        add_library(${name} MODULE tests/clap/sine_plugin_test.cpp)
        target_link_libraries(${name} PRIVATE clap)
        set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick Test ${variant}"
            LIBRARY_OUTPUT_DIRECTORY ${test_clap_folder})
        if(APPLE)
            set_target_properties(${name} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
        endif()
    endforeach()
    target_compile_definitions(emerick_test_crash PRIVATE EMERICK_TEST_CRASH)
    target_compile_definitions(emerick_test_hang PRIVATE EMERICK_TEST_HANG)
    # Test effects as CLAP bundles in their own folder: a gain, and a copy that delays its output and reports it.
    set(test_clap_effect_folder ${CMAKE_BINARY_DIR}/test-clap-effect)
    foreach(variant gain late)
        set(name emerick_test_clap_${variant})
        add_library(${name} MODULE tests/clap/gain_plugin_test.cpp)
        target_link_libraries(${name} PRIVATE clap)
        set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick Test ${variant}"
            LIBRARY_OUTPUT_DIRECTORY ${test_clap_effect_folder})
        if(APPLE)
            set_target_properties(${name} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
        endif()
    endforeach()
    target_compile_definitions(emerick_test_clap_late PRIVATE EMERICK_TEST_LATENCY)
    if(APPLE)
        set(test_sine_clap "$<TARGET_BUNDLE_DIR:emerick_test_sine>")
    else()
        set(test_sine_clap "$<TARGET_FILE:emerick_test_sine>")
    endif()
    add_library(emerick_test_clap INTERFACE)
    add_dependencies(emerick_test_clap emerick_test_sine emerick_test_crash emerick_test_hang emerick_test_clap_gain
        emerick_test_clap_late emerick-plugin-scanner)
    target_compile_definitions(emerick_test_clap INTERFACE EMERICK_TEST_CLAP_FOLDER="${test_clap_folder}"
        EMERICK_TEST_CLAP_EFFECT_FOLDER="${test_clap_effect_folder}"
        EMERICK_TEST_SINE_CLAP="${test_sine_clap}" EMERICK_PLUGIN_SCANNER="$<TARGET_FILE:emerick-plugin-scanner>")

    # Test VST3 bundles: a gain effect, copies whose module entry crashes or hangs, a crashing copy whose
    # moduleinfo.json describes it, so a scan never loads it, and a sine instrument.
    set(test_vst3_folder ${CMAKE_BINARY_DIR}/test-vst3)
    foreach(variant gain crash hang described synth)
        set(name emerick_test_vst3_${variant})
        set(bundle "${test_vst3_folder}/Emerick Test ${variant}.vst3")
        if(variant STREQUAL synth)
            add_library(${name} MODULE tests/vst3/synth_plugin_test.cpp)
        else()
            add_library(${name} MODULE tests/vst3/gain_plugin_test.cpp)
        endif()
        target_link_libraries(${name} PRIVATE vst3_sdk)
        set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME "Emerick Test ${variant}"
            CXX_VISIBILITY_PRESET hidden)
        if(APPLE)
            set_target_properties(${name} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION vst3
                LIBRARY_OUTPUT_DIRECTORY ${test_vst3_folder})
        elseif(WIN32)
            set_target_properties(${name} PROPERTIES SUFFIX ".vst3"
                LIBRARY_OUTPUT_DIRECTORY "${bundle}/Contents/x86_64-win")
        else()
            set_target_properties(${name} PROPERTIES SUFFIX ".so"
                LIBRARY_OUTPUT_DIRECTORY "${bundle}/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux")
        endif()
    endforeach()
    target_compile_definitions(emerick_test_vst3_crash PRIVATE EMERICK_TEST_CRASH)
    target_compile_definitions(emerick_test_vst3_hang PRIVATE EMERICK_TEST_HANG)
    target_compile_definitions(emerick_test_vst3_described PRIVATE EMERICK_TEST_CRASH)
    set(resources "${test_vst3_folder}/Emerick Test described.vst3/Contents/Resources")
    configure_file(tests/vst3/moduleinfo.json "${resources}/moduleinfo.json" COPYONLY)
    add_library(emerick_test_vst3 INTERFACE)
    add_dependencies(emerick_test_vst3 emerick_test_vst3_gain emerick_test_vst3_crash emerick_test_vst3_hang
        emerick_test_vst3_described emerick_test_vst3_synth emerick-plugin-scanner)
    target_compile_definitions(emerick_test_vst3 INTERFACE EMERICK_TEST_VST3_FOLDER="${test_vst3_folder}"
        EMERICK_TEST_GAIN_VST3="${test_vst3_folder}/Emerick Test gain.vst3"
        EMERICK_TEST_DESCRIBED_VST3="${test_vst3_folder}/Emerick Test described.vst3"
        EMERICK_TEST_SYNTH_VST3="${test_vst3_folder}/Emerick Test synth.vst3")
    target_link_libraries(emerick_engine_tests PRIVATE emerick_engine emerick_test_clap emerick_test_vst3 emerick_gm_clap emerick_gm_midi PkgConfig::SNDFILE)
endif()
