add_library(emerick_engine
    src/audio_backend.cpp
    src/clip_stream.cpp
    src/engine.cpp
    src/interleaved_io.cpp
    src/libremidi_backend.cpp
    src/metronome_sounds.cpp
    src/midi_ports.cpp
    src/rt_metronome.cpp
    src/rt_midi.cpp
    src/rt_process.cpp
    src/snapshot_builder.cpp
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
    PRIVATE libremidi readerwriterqueue PkgConfig::SNDFILE)
emerick_target(emerick_engine)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_engine_tests tests/engine_test.cpp tests/fade_test.cpp tests/interleaved_io_test.cpp
        tests/metronome_test.cpp
        tests/recording_test.cpp tests/stream_health_test.cpp)
    target_include_directories(emerick_engine_tests PRIVATE src)
    target_link_libraries(emerick_engine_tests PRIVATE emerick_engine PkgConfig::SNDFILE)
endif()
