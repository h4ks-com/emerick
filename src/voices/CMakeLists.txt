add_library(emerick_voices_core STATIC store.cpp wave.cpp rt_sampler.cpp)
target_include_directories(emerick_voices_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(emerick_voices_core PRIVATE pugixml::pugixml)
set_target_properties(emerick_voices_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
emerick_target(emerick_voices_core)

add_library(emerick_voices MODULE voices_plugin.cpp voices_library.cpp voices_settings.cpp)
target_link_libraries(emerick_voices PRIVATE emerick_voices_core clap nlohmann_json::nlohmann_json)
set_target_properties(emerick_voices PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick Voices"
    LIBRARY_OUTPUT_DIRECTORY ${emerick_bundled_clap_folder})
if(APPLE)
    set_target_properties(emerick_voices PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
    set(voices_bundle "$<TARGET_BUNDLE_DIR:emerick_voices>")
else()
    set(voices_bundle "$<TARGET_FILE:emerick_voices>")
endif()
emerick_target(emerick_voices)

add_library(emerick_voices_clap INTERFACE)
add_dependencies(emerick_voices_clap emerick_voices)
target_compile_definitions(emerick_voices_clap INTERFACE EMERICK_VOICES_CLAP="${voices_bundle}")

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_voices_tests tests/store_test.cpp tests/voices_test.cpp tests/synthetic_store.cpp)
    target_link_libraries(emerick_voices_tests PRIVATE emerick_voices_core emerick_engine emerick_voices_clap
        nlohmann_json::nlohmann_json)
endif()
