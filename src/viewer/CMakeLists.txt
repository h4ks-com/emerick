qt_add_library(emerick_viewer_lib STATIC
    remote_peaks.cpp
    remote_peaks.hpp
    silent_meters.cpp
    silent_meters.hpp
    viewer.cpp
    viewer.hpp
    viewer_project.cpp
    viewer_project.hpp
    viewer_registry.cpp
    viewer_registry.hpp
    viewer_tracks.cpp
    viewer_tracks.hpp)
target_include_directories(emerick_viewer_lib PUBLIC .)
qt_add_qml_module(emerick_viewer_lib URI Emerick.Viewer VERSION 1.0 QML_FILES Viewer.qml BlockRow.qml)
target_link_libraries(emerick_viewer_lib PUBLIC Qt6::Qml Qt6::Network emerick_view_data PRIVATE emerick_yamaha_codec)
emerick_target(emerick_viewer_lib)

qt_add_executable(emerick_viewer main.cpp)
target_link_libraries(emerick_viewer PRIVATE emerick_viewer_lib emerick_viewer_libplugin emerick_uiplugin
    Qt6::QuickControls2)
emerick_target(emerick_viewer)
if(EMSCRIPTEN)
    configure_file(web/index.html index.html COPYONLY)
    # The import scanner reads every view in Emerick.Ui, and the app's file dialogs pull in every Controls style, so we
    # link only the QML plugins the tracks view imports.
    set_target_properties(emerick_viewer PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)
    target_link_libraries(emerick_viewer PRIVATE Qt6::qmlplugin Qt6::modelsplugin Qt6::qtquick2plugin
        Qt6::quickwindow Qt6::qtquicktemplates2plugin Qt6::qtquickcontrols2implplugin
        Qt6::qtquickcontrols2basicstyleplugin Qt6::qtquickcontrols2basicstyleimplplugin Qt6::qquicklayoutsplugin
        Qt6::qmlshapesplugin)
endif()

if(EMERICK_BUILD_TESTS AND NOT EMSCRIPTEN)
    emerick_test(emerick_viewer_tests tests/viewer_test.cpp)
    target_include_directories(emerick_viewer_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_viewer_tests
        PRIVATE emerick_viewer_lib emerick_viewer_libplugin emerick_uiplugin Qt6::QuickControls2 Qt6::Test)
endif()
