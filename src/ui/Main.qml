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
            enabled: !(window.activeFocusItem instanceof TextInput)
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
        ChromeButton {
            objectName: "view.toggleInspector"
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            iconName: "sidebar-simple"
            active: Nav.inspectorOpen
            activeColor: Theme.ink
        }
        Row {
            anchors.centerIn: parent
            spacing: Theme.space3
            TransportBar {
                anchors.verticalCenter: parent.verticalCenter
            }
            TransportDisplay {
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            ChromeButton {
                objectName: "track.addAudio"
                iconName: "waveform"
            }
            ChromeButton {
                objectName: "track.addMidi"
                iconName: "piano-keys"
            }
            ChromeButton {
                objectName: "palette.open"
                iconName: "magnifying-glass"
            }
            ChromeButton {
                objectName: "settings.open"
                iconName: "gear-six"
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        visible: Nav.page === App.Main
        spacing: 0
        Inspector {
            visible: Nav.inspectorOpen
            Layout.preferredWidth: Theme.inspectorWidth
            Layout.fillHeight: true
        }
        Rectangle {
            visible: Nav.inspectorOpen
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
