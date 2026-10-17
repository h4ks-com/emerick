add_library(emerick_engine
    src/audio_backend.cpp
    src/clip_stream.cpp
    src/engine.cpp
    src/libremidi_backend.cpp
    src/midi_ports.cpp
    src/miniaudio_backend.cpp
    src/rt_midi.cpp
    src/rt_process.cpp
    src/snapshot_builder.cpp
    src/take_recorder.cpp
    src/virtual_midi_backend.cpp)
if(APPLE)
    target_sources(emerick_engine PRIVATE src/private_aggregate_apple.cpp)
    target_link_libraries(emerick_engine PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
endif()
target_include_directories(emerick_engine PUBLIC include)
target_link_libraries(emerick_engine
    PUBLIC emerick_core
    PRIVATE miniaudio libremidi readerwriterqueue PkgConfig::SNDFILE)
emerick_target(emerick_engine)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_engine_tests tests/engine_test.cpp tests/recording_test.cpp)
    target_link_libraries(emerick_engine_tests PRIVATE emerick_engine PkgConfig::SNDFILE)
endif()
