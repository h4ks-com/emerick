qt_add_executable(emerick MACOSX_BUNDLE main.cpp icons/emerick.icns)
set_source_files_properties(icons/emerick.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_resources(emerick app_icon PREFIX / FILES icons/emerick.png)
# Info.plist declares .erk bundles as Emerick document packages, so Finder shows one as a file.
set_target_properties(emerick PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME Emerick
    MACOSX_BUNDLE_GUI_IDENTIFIER org.emerick.app
    MACOSX_BUNDLE_ICON_FILE emerick.icns)
emerick_target(emerick)
# The app runs the plugin scanner from its own folder and always scans the CLAP bundles it ships.
add_dependencies(emerick emerick-clap-scanner emerick_gm)
if(APPLE)
    set(bundled_clap_folder "$<TARGET_BUNDLE_CONTENT_DIR:emerick>/PlugIns")
else()
    set(bundled_clap_folder "$<TARGET_FILE_DIR:emerick>/clap")
endif()
add_custom_command(TARGET emerick POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:emerick-clap-scanner> $<TARGET_FILE_DIR:emerick>
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "$<TARGET_PROPERTY:emerick_gm,LIBRARY_OUTPUT_DIRECTORY>"
        ${bundled_clap_folder})
target_link_libraries(emerick PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick emerick_app emerick_uiplugin emerick_yamaha_plugin
    emerick_yamaha_pluginplugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_reachability_tests tests/action_reachability_test.cpp)
    target_include_directories(emerick_reachability_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_reachability_tests PRIVATE emerick_yamaha_plugin)
    target_compile_definitions(emerick_reachability_tests PRIVATE EMERICK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
