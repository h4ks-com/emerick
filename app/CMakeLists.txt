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
add_dependencies(emerick emerick-plugin-scanner emerick_gm emerick_voices emerick_sampler emerick_arranger_device)
if(APPLE)
    set(bundled_clap_folder "$<TARGET_BUNDLE_CONTENT_DIR:emerick>/PlugIns")
    set(notices_folder "$<TARGET_BUNDLE_CONTENT_DIR:emerick>/Resources")
else()
    set(bundled_clap_folder "$<TARGET_FILE_DIR:emerick>/clap")
    set(notices_folder ${bundled_clap_folder})
endif()
# A target of its own runs on every build, so a rebuilt plugin reaches the app even when the app itself did not relink.
add_custom_target(emerick_bundled_plugins ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:emerick-plugin-scanner> $<TARGET_FILE_DIR:emerick>
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${emerick_bundled_clap_folder} ${bundled_clap_folder}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/third-party-notices.txt
        ${PROJECT_SOURCE_DIR}/LICENSE ${notices_folder})
add_dependencies(emerick_bundled_plugins emerick)
target_link_libraries(emerick PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick emerick_app emerick_uiplugin emerick_yamaha_plugin
    emerick_yamaha_pluginplugin emerick_broadcast_plugin emerick_broadcast_pluginplugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_reachability_tests tests/action_reachability_test.cpp)
    target_include_directories(emerick_reachability_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_reachability_tests PRIVATE emerick_yamaha_plugin emerick_broadcast_plugin)
    target_compile_definitions(emerick_reachability_tests PRIVATE EMERICK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
