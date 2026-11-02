import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui

Rectangle {
    id: viewer
    color: Theme.graphite

    ActionShortcuts {}

    GlassPanel {
        id: controlBar
        width: parent.width
        height: Theme.headerHeight
        radius: 0
        border.width: 0
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            ChromeButton {
                objectName: "view.toggleMixer"
                iconName: "sliders"
                active: Nav.mixerOpen
                activeColor: Theme.ink
            }
            ChromeButton {
                objectName: "view.togglePianoRoll"
                iconName: "music-notes"
                active: Nav.pianoRollOpen
                activeColor: Theme.ink
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: Theme.space1
                text: Project.name
                color: Theme.inkDim
                font.pixelSize: Theme.textLabel
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Theme.labelWidth)
            }
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
    }

    TrackList {
        anchors.top: controlBar.bottom
        anchors.bottom: pianoRollDock.top
        width: parent.width
    }

    // The open docks split the app's share of the height between them.
    readonly property int openDocks: (pianoRollDock.active ? 1 : 0) + (mixerDock.active ? 1 : 0)
    component Dock: Loader {
        property real panelHeight
        width: parent.width
        height: active ? Math.min(panelHeight, (viewer.height - controlBar.height) * Theme.dockShare / viewer.openDocks) : 0
        function resizeBy(delta) {
            panelHeight = Math.max(Theme.pianoRollMinHeight, panelHeight + delta)
        }
    }

    Dock {
        id: pianoRollDock
        panelHeight: Theme.pianoRollHeight
        anchors.bottom: mixerDock.top
        active: Nav.pianoRollOpen
        sourceComponent: PianoRoll {
            onResizeBy: delta => pianoRollDock.resizeBy(delta)
        }
    }

    Dock {
        id: mixerDock
        panelHeight: Theme.mixerHeight
        anchors.bottom: parent.bottom
        active: Nav.mixerOpen
        sourceComponent: Mixer {
            onResizeBy: delta => mixerDock.resizeBy(delta)
        }
    }
}
