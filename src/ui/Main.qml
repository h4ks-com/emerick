import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

ApplicationWindow {
    id: window
    objectName: "window.main"
    width: Theme.windowWidth
    height: Theme.windowHeight
    visible: true
    title: "Emerick"
    color: Theme.graphite

    Instantiator {
        model: Registry.shortcuts
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.shortcut
            context: Qt.ApplicationShortcut
            onActivated: Registry.activate(modelData.id)
        }
    }

    header: GlassPanel {
        height: Theme.headerHeight
        radius: 0
        border.width: 0
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space3
            anchors.rightMargin: Theme.space3
            spacing: Theme.space2
            ChromeButton {
                objectName: "transport.play"
                active: Project.playing && !Project.recording
            }
            ChromeButton {
                objectName: "transport.stop"
            }
            ChromeButton {
                objectName: "transport.record"
                active: Project.recording
                activeColor: Theme.rec
            }
            Label {
                objectName: "transport.position"
                text: Project.positionText
                color: Theme.tape
                font.pixelSize: Theme.textCounter
                font.weight: Font.Light
                font.features: {"tnum": 1}
                Layout.leftMargin: Theme.space4
            }
            Label {
                text: Project.tempoText
                color: Theme.inkDim
                font.pixelSize: Theme.textBody
                font.features: {"tnum": 1}
                Layout.leftMargin: Theme.space4
                Layout.fillWidth: true
            }
            ChromeButton {
                objectName: "track.addAudio"
            }
            ChromeButton {
                objectName: "track.addMidi"
            }
            ChromeButton {
                objectName: "settings.open"
            }
            ChromeButton {
                objectName: "palette.open"
                text: shortcutText
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        visible: Nav.page === App.Main
        spacing: 0
        Inspector {
            Layout.preferredWidth: Theme.inspectorWidth
            Layout.fillHeight: true
        }
        Rectangle {
            Layout.preferredWidth: Theme.hairline
            Layout.fillHeight: true
            color: Theme.rule
        }
        TrackList {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Loader {
        anchors.fill: parent
        active: Nav.page === App.Settings
        sourceComponent: SettingsPage {}
    }

    CommandPalette {}

    Popup {
        anchors.centerIn: parent
        padding: Theme.space5
        modal: true
        visible: Nav.aboutOpen
        onClosed: Nav.aboutOpen = false
        background: GlassPanel {}
        contentItem: Column {
            objectName: "about"
            spacing: Theme.space2
            Label {
                text: "Emerick " + Qt.application.version
                color: Theme.ink
                font.pixelSize: Theme.textTitle
            }
            Label {
                text: "A small DAW for recording instruments. GPL-3.0-or-later."
                color: Theme.inkDim
                font.pixelSize: Theme.textLabel
            }
        }
    }
}
