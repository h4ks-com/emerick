import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A channel strip per track, left to right in track order.
DockPanel {
    id: panel
    objectName: "mixer"

    ListView {
        id: strips
        objectName: "mixer.strips"
        anchors.fill: parent
        anchors.topMargin: Theme.hairline
        orientation: ListView.Horizontal
        interactive: false
        clip: true
        model: Tracks
        ScrollBar.horizontal: ScrollBar {}
        delegate: MixerStrip {
            height: ListView.view.height
        }
    }

    ToggleSweep {
        objectName: "mixer.toggleSweep"
        anchors.fill: strips
        target: strips
    }

    Label {
        anchors.centerIn: parent
        visible: strips.count === 0
        text: "Add a track to mix it"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
}
