add_library(emerick_arranger arranger.cpp casm.cpp style.cpp transpose.cpp)
target_include_directories(emerick_arranger PUBLIC include)
target_link_libraries(emerick_arranger PUBLIC emerick_core emerick_yamaha_codec)
emerick_target(emerick_arranger)
# The Arranger CLAP bundle links these into a loadable module.
set_target_properties(emerick_arranger emerick_core emerick_yamaha_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(emerick_arranger_device MODULE clap/arranger_plugin.cpp clap/player.cpp)
target_link_libraries(emerick_arranger_device PRIVATE emerick_arranger clap nlohmann_json::nlohmann_json)
set_target_properties(emerick_arranger_device PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick Arranger"
    LIBRARY_OUTPUT_DIRECTORY ${emerick_bundled_clap_folder})
if(APPLE)
    set_target_properties(emerick_arranger_device PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
    set(arranger_bundle "$<TARGET_BUNDLE_DIR:emerick_arranger_device>")
else()
    set(arranger_bundle "$<TARGET_FILE:emerick_arranger_device>")
endif()
emerick_target(emerick_arranger_device)

add_library(emerick_arranger_clap INTERFACE)
add_dependencies(emerick_arranger_clap emerick_arranger_device)
target_compile_definitions(emerick_arranger_clap INTERFACE EMERICK_ARRANGER_CLAP="${arranger_bundle}")

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_arranger_tests
        tests/arranger_test.cpp
        tests/style_test.cpp
        tests/transpose_test.cpp)
    target_link_libraries(emerick_arranger_tests PRIVATE emerick_arranger)

    emerick_test(emerick_arranger_device_tests tests/device_test.cpp)
    target_link_libraries(emerick_arranger_device_tests PRIVATE emerick_engine emerick_arranger_clap
        nlohmann_json::nlohmann_json)
endif()
