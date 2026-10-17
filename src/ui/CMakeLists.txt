qt_add_library(emerick_ui STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(emerick_ui
    URI Emerick.Ui
    VERSION 1.0
    QML_FILES
        ChromeButton.qml
        ClipBody.qml
        ClipItem.qml
        CommandPalette.qml
        GlassPanel.qml
        Inspector.qml
        Main.qml
        RouteCombo.qml
        Ruler.qml
        SettingsPage.qml
        Theme.qml
        TrackHeader.qml
        TrackList.qml
    RESOURCES
        icons/guitar.svg
        icons/music-note.svg
        icons/music-notes.svg
        icons/piano-keys.svg
        icons/squares-four.svg
        icons/waveform.svg)

target_link_libraries(emerick_ui PRIVATE Qt6::Quick Qt6::QuickControls2)

if(EMERICK_BUILD_TESTS)
    emerick_test(emerick_ui_tests tests/timeline_test.cpp)
    target_include_directories(emerick_ui_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
    target_link_libraries(emerick_ui_tests PRIVATE emerick_app emerick_uiplugin Qt6::Quick Qt6::Test)
endif()
