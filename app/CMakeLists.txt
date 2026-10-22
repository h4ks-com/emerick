qt_add_executable(emerick MACOSX_BUNDLE main.cpp)
# Info.plist declares .erk bundles as Emerick document packages, so Finder shows one as a file.
set_target_properties(emerick PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME Emerick
    MACOSX_BUNDLE_GUI_IDENTIFIER org.emerick.app)
emerick_target(emerick)
target_link_libraries(emerick PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick emerick_app emerick_uiplugin emerick_yamaha_plugin
    emerick_yamaha_pluginplugin)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_reachability_tests tests/action_reachability_test.cpp)
    target_include_directories(emerick_reachability_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_reachability_tests PRIVATE emerick_yamaha_plugin)
    target_compile_definitions(emerick_reachability_tests PRIVATE EMERICK_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
