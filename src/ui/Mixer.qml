import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A channel strip per track, left to right in track order, then a strip per bus.
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
        }
    }

    Flickable {
        id: flick
        objectName: "mixer.strips"
        anchors.fill: parent
        anchors.topMargin: Theme.hairline
        anchors.rightMargin: analyzeAll.width + 2 * Theme.space1
        contentWidth: row.width
        contentHeight: row.height
        interactive: false
        clip: true
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        Row {
            id: row
            readonly property real stripHeight: Math.max(flick.height, ...children.map(child => child.implicitHeight))
            Strips { buses: false }
            Strips { buses: true }
        }
    }

    ChromeButton {
        id: analyzeAll
        objectName: "tracks.analyze"
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
        anchors.centerIn: parent
        visible: row.width === 0
        text: "Add a track to mix it"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
}
