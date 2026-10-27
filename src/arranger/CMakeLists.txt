add_library(emerick_arranger arranger.cpp casm.cpp style.cpp transpose.cpp)
target_include_directories(emerick_arranger PUBLIC include)
target_link_libraries(emerick_arranger PUBLIC emerick_core emerick_yamaha_codec)
emerick_target(emerick_arranger)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_arranger_tests
        tests/arranger_test.cpp
        tests/style_test.cpp
        tests/transpose_test.cpp)
    target_link_libraries(emerick_arranger_tests PRIVATE emerick_arranger)
endif()
