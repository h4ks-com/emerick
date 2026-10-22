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
            height: flick.height
        }
    }

    Flickable {
        id: flick
        objectName: "mixer.strips"
        anchors.fill: parent
        anchors.topMargin: Theme.hairline
        contentWidth: row.width
        interactive: false
        clip: true
        ScrollBar.horizontal: ScrollBar {}

        Row {
            id: row
            height: flick.height
            Strips { buses: false }
            Strips { buses: true }
        }
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
