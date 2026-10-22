qt_add_library(emerick_ui STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(emerick_ui
    URI Emerick.Ui
    VERSION 1.0
    QML_FILES
        ActionMenu.qml
        ActionMenuItem.qml
        AudioRoutePicker.qml
        AutomationLane.qml
        AutomationPicker.qml
        ChromeButton.qml
        ClipBody.qml
        ClipItem.qml
        CommandPalette.qml
        ConsolePanel.qml
        DockPanel.qml
        DragGroup.qml
        DragNumber.qml
        FlagButton.qml
        GainReadout.qml
        GainSlider.qml
        GlassMenu.qml
        GlassMenuItem.qml
        GlassPanel.qml
        HealthPanel.qml
        Inspector.qml
        KeyCommands.qml
        LevelMeter.qml
        Main.qml
        MarkerLane.qml
        Mixer.qml
        MixerStrip.qml
        PanKnob.qml
        PanNumber.qml
        PianoGrid.qml
        PianoKeyboard.qml
        PianoRoll.qml
        RouteCombo.qml
        Ruler.qml
        SearchCombo.qml
        SendList.qml
        SettingsPage.qml
        Theme.qml
        ToggleSweep.qml
        TrackHeader.qml
        TrackList.qml
        TransportBar.qml
        TransportDisplay.qml
        VelocityLane.qml
    RESOURCES
        icons/arrows-merge.svg
        icons/automation.svg
        icons/broom.svg
        icons/circle.svg
        icons/copy.svg
        icons/dots-three.svg
        icons/fade-equalPower.svg
        icons/fade-exponential.svg
        icons/fade-linear.svg
        icons/fade-logarithmic.svg
        icons/fade-sCurve.svg
        icons/file.svg
        icons/gear-six.svg
        icons/guitar.svg
        icons/magnet.svg
        icons/magnifying-glass.svg
        icons/metronome.svg
        icons/music-note.svg
        icons/music-notes.svg
        icons/pause.svg
        icons/pencil-simple.svg
        icons/piano-keys.svg
        icons/play.svg
        icons/plus.svg
        icons/sidebar-simple.svg
        icons/skip-back.svg
        icons/sliders.svg
        icons/squares-four.svg
        icons/stop.svg
        icons/waveform.svg
        icons/x.svg)

target_link_libraries(emerick_ui PRIVATE Qt6::Quick Qt6::QuickControls2)

if(EMERICK_BUILD_TESTS)
    # QML singletons register once per process, so each test that loads a view gets its own binary.
    foreach(name automation console editing file_menu meter mixer picker piano_roll timeline tooltips transport_bar value_controls)
        emerick_test(emerick_ui_${name}_tests tests/${name}_test.cpp)
        target_include_directories(emerick_ui_${name}_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/tests)
        target_link_libraries(emerick_ui_${name}_tests PRIVATE emerick_app emerick_uiplugin Qt6::Quick Qt6::Test)
    endforeach()
endif()
