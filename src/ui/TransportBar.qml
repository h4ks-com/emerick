import QtQuick
import Emerick.App

GlassPanel {
    implicitWidth: buttons.implicitWidth + 2 * Theme.space1
    implicitHeight: Theme.displayHeight

    component TransportButton: ChromeButton {
        implicitWidth: Theme.transportButton
        implicitHeight: Theme.transportButton
    }

    Row {
        id: buttons
        anchors.centerIn: parent
        spacing: Theme.space1 / 2
        TransportButton {
            objectName: "transport.gotoStart"
            iconName: "skip-back"
        }
        TransportButton {
            objectName: "transport.togglePlay"
            iconName: Project.playing ? "pause" : "play"
            active: Project.playing && !Project.recording
        }
        TransportButton {
            objectName: "transport.record"
            iconName: "circle"
            iconColor: Theme.rec
            active: Project.recording
            activeColor: Theme.rec
        }
        TransportButton {
            objectName: "transport.stop"
            iconName: "stop"
        }
    }
}
