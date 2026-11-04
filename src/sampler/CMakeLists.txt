add_library(emerick_sampler MODULE sampler_plugin.cpp)
target_link_libraries(emerick_sampler PRIVATE sfizz::sfizz clap nlohmann_json::nlohmann_json)
set_target_properties(emerick_sampler PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick Sampler"
    LIBRARY_OUTPUT_DIRECTORY ${emerick_bundled_clap_folder})
if(APPLE)
    set_target_properties(emerick_sampler PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
    set(sampler_bundle "$<TARGET_BUNDLE_DIR:emerick_sampler>")
else()
    set(sampler_bundle "$<TARGET_FILE:emerick_sampler>")
endif()
emerick_target(emerick_sampler)

add_library(emerick_sampler_params INTERFACE)
target_include_directories(emerick_sampler_params INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(emerick_sampler_clap INTERFACE)
add_dependencies(emerick_sampler_clap emerick_sampler)
target_compile_definitions(emerick_sampler_clap INTERFACE EMERICK_SAMPLER_CLAP="${sampler_bundle}")

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_sampler_tests tests/sampler_test.cpp)
    target_link_libraries(emerick_sampler_tests PRIVATE emerick_engine emerick_sampler_clap emerick_sampler_params
        nlohmann_json::nlohmann_json)
    target_compile_definitions(emerick_sampler_tests PRIVATE EMERICK_SAMPLER_SINE="${CMAKE_CURRENT_SOURCE_DIR}/tests/sine")
endif()
