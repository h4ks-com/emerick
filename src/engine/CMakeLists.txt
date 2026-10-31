add_library(emerick_engine
    src/audio_analysis.cpp
    src/audio_backend.cpp
    src/clap_instrument.cpp
    src/clap_library.cpp
    src/clap_params.cpp
    src/clap_scan.cpp
    src/clip_stream.cpp
    src/effects.cpp
    src/engine.cpp
    src/interleaved_io.cpp
    src/libremidi_backend.cpp
    src/loudness_match.cpp
    src/metronome_sounds.cpp
    src/midi_ports.cpp
    src/rt_clap.cpp
    src/rt_metronome.cpp
    src/rt_effects.cpp
    src/rt_limiter.cpp
    src/rt_midi.cpp
    src/rt_process.cpp
    src/snapshot_builder.cpp
    src/spectrum.cpp
    src/take_recorder.cpp
    src/virtual_midi_backend.cpp)
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
    PRIVATE libremidi readerwriterqueue PkgConfig::SNDFILE clap-helpers ${CMAKE_DL_LIBS})
emerick_target(emerick_engine)

add_executable(emerick-clap-scanner scanner/clap_scanner.cpp)
target_link_libraries(emerick-clap-scanner PRIVATE emerick_engine nlohmann_json::nlohmann_json)
emerick_target(emerick-clap-scanner)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_engine_tests tests/audio_analysis_test.cpp tests/automation_test.cpp tests/bus_test.cpp tests/clap_test.cpp tests/effects_test.cpp tests/engine_test.cpp tests/fade_test.cpp tests/gm_test.cpp tests/interleaved_io_test.cpp
        tests/metronome_test.cpp tests/cycle_test.cpp tests/midi_chase_test.cpp tests/midi_route_test.cpp
        tests/recording_test.cpp tests/spectrum_test.cpp tests/stream_health_test.cpp tests/take_progress_test.cpp)
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
    if(APPLE)
        set(test_sine_clap "$<TARGET_BUNDLE_DIR:emerick_test_sine>")
    else()
        set(test_sine_clap "$<TARGET_FILE:emerick_test_sine>")
    endif()
    add_library(emerick_test_clap INTERFACE)
    add_dependencies(emerick_test_clap emerick_test_sine emerick_test_crash emerick_test_hang emerick-clap-scanner)
    target_compile_definitions(emerick_test_clap INTERFACE EMERICK_TEST_CLAP_FOLDER="${test_clap_folder}"
        EMERICK_TEST_SINE_CLAP="${test_sine_clap}" EMERICK_CLAP_SCANNER="$<TARGET_FILE:emerick-clap-scanner>")
    target_link_libraries(emerick_engine_tests PRIVATE emerick_engine emerick_test_clap emerick_gm_clap emerick_gm_midi PkgConfig::SNDFILE)
endif()
