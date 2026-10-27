add_library(emerick_gm MODULE gm_plugin.cpp gm_settings.cpp)
target_link_libraries(emerick_gm PRIVATE clap nlohmann_json::nlohmann_json PkgConfig::FLUIDSYNTH)
set_target_properties(emerick_gm PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Emerick GM"
    LIBRARY_OUTPUT_DIRECTORY ${emerick_bundled_clap_folder})
if(APPLE)
    set_target_properties(emerick_gm PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
    set(gm_resources "$<TARGET_BUNDLE_CONTENT_DIR:emerick_gm>/Resources")
    set(gm_bundle "$<TARGET_BUNDLE_DIR:emerick_gm>")
else()
    set(gm_resources "$<TARGET_FILE_DIR:emerick_gm>")
    set(gm_bundle "$<TARGET_FILE:emerick_gm>")
endif()
emerick_target(emerick_gm)
add_custom_command(TARGET emerick_gm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${gm_resources}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${generaluser_gs_SOURCE_DIR}/GeneralUser-GS.sf2 ${gm_resources}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${generaluser_gs_license_SOURCE_DIR}/LICENSE.txt
        ${gm_resources}/GeneralUser-GS-LICENSE.txt)

add_library(emerick_gm_clap INTERFACE)
add_dependencies(emerick_gm_clap emerick_gm)
target_compile_definitions(emerick_gm_clap INTERFACE EMERICK_GM_CLAP="${gm_bundle}"
    EMERICK_GM_APP_FOLDER="${emerick_bundled_app_folder}")
