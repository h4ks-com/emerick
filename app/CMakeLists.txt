qt_add_executable(emerick MACOSX_BUNDLE main.cpp)
# Info.plist declares .erk bundles as Emerick document packages, so Finder shows one as a file.
set_target_properties(emerick PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME Emerick
    MACOSX_BUNDLE_GUI_IDENTIFIER org.emerick.app)
emerick_target(emerick)
target_link_libraries(emerick PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick emerick_app emerick_uiplugin emerick_yamaha_plugin
    emerick_yamaha_pluginplugin)
