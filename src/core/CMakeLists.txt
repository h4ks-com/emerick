add_library(emerick_core
    base64.cpp
    fade.cpp
    fuzzy.cpp
    id.cpp
    time.cpp)
target_include_directories(emerick_core PUBLIC include)
emerick_target(emerick_core)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_core_tests tests/base64_test.cpp tests/fuzzy_test.cpp tests/join_test.cpp tests/time_test.cpp)
    target_link_libraries(emerick_core_tests PRIVATE emerick_core)
endif()
