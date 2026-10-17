add_library(emerick_yamaha_codec
    src/chart.cpp
    src/chord.cpp
    src/profile.cpp
    src/render.cpp
    src/sysex.cpp)
target_include_directories(emerick_yamaha_codec PUBLIC include)
target_link_libraries(emerick_yamaha_codec PUBLIC emerick_core nlohmann_json::nlohmann_json)
emerick_target(emerick_yamaha_codec)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_yamaha_codec_tests
        tests/chart_test.cpp
        tests/chord_test.cpp
        tests/profile_test.cpp
        tests/render_test.cpp
        tests/sysex_test.cpp)
    target_link_libraries(emerick_yamaha_codec_tests PRIVATE emerick_yamaha_codec libremidi)
    target_compile_definitions(emerick_yamaha_codec_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
