import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A channel strip per track, left to right in track order, then a strip per bus, then the master's output meter.
DockPanel {
    id: panel
    objectName: "mixer"

    component Strips: Repeater {
        id: strips
        property bool buses
        model: Tracks
        delegate: MixerStrip {
            visible: busRoutes === strips.buses
            height: row.stripHeight
            listsHeight: row.listsHeight
        }
    }

    Flickable {
        id: flick
        objectName: "mixer.strips"
        anchors.fill: parent
        anchors.topMargin: Theme.hairline
        anchors.rightMargin: analyzeAll.visible ? analyzeAll.width + 2 * Theme.space1 : 0
        contentWidth: row.width
        contentHeight: row.height
        interactive: false
        clip: true
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        Row {
            id: row
            readonly property real listsHeight: Math.max(0, ...children.map(child => child.listsImplicitHeight ?? 0))
            readonly property real stripHeight: Math.max(flick.height, ...children.map(child => child.minimumHeight ?? 0))
            Strips {
                id: trackStrips
                buses: false
            }
            Strips { buses: true }

            Rectangle {
                objectName: "mixer.master"
                width: Registry.settings["view.stripWidth"]
                height: row.stripHeight
                border.width: Theme.hairline
                border.color: Theme.rule
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.lift }
                    GradientStop { position: 1; color: Theme.slate }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space1
                    spacing: Theme.space1

                    LevelMeter {
                        trackId: "master"
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: Theme.faderCapHeight / 2
                    }
                    Label {
                        text: "Master"
                        color: Theme.ink
                        font.pixelSize: Theme.textLabel
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    ChromeButton {
        id: analyzeAll
        objectName: "tracks.analyze"
        visible: !Registry.readOnly
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.space1
        iconName: "chart-bar"
        iconColor: Theme.inkDim
        active: Analysis.busyTrack !== ""
        activeColor: Theme.inkDim
        hint: active ? "Wait while the tracks render" : "Render every track as its stem and show its active RMS on its strip"
        run: () => Analysis.analyzeAll()
    }

    ToggleSweep {
        objectName: "mixer.toggleSweep"
        anchors.fill: flick
        target: flick
    }

    Label {
        objectName: "mixer.empty"
        anchors.centerIn: parent
        // Each Strips repeater holds a delegate per row of Tracks, buses included, so one count covers the project.
        visible: trackStrips.count === 0
        text: "Add a track to mix it"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
}
