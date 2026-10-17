qt_add_library(emerick_ui STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(emerick_ui
    URI Emerick.Ui
    VERSION 1.0
    QML_FILES
        ChromeButton.qml
        CommandPalette.qml
        GlassPanel.qml
        Inspector.qml
        Main.qml
        RouteCombo.qml
        SettingsPage.qml
        Theme.qml
        TrackList.qml)

target_link_libraries(emerick_ui PRIVATE Qt6::Quick Qt6::QuickControls2)
