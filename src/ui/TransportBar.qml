import QtQuick
import QtQuick.Controls.Basic
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
        TransportButton {
            objectName: "cycle.toggle"
            iconName: "repeat"
            active: Project.cycle.enabled
        }
        TransportButton {
            objectName: "transport.panic"
            iconName: "warning-octagon"
        }
        TransportButton {
            id: metronomeButton
            objectName: "metronome.toggle"
            iconName: "metronome"
            active: Registry.settings["metronome.enabled"]
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: mouse => {
                    if (Theme.isContextClick(mouse)) {
                        Registry.activate("metronome.showMenu")
                    } else {
                        mouse.accepted = false
                    }
                }
            }
            ActionMenu {
                objectName: "menu.metronome"
                menuName: "Metronome"
                y: metronomeButton.height + Theme.space1
                visible: Nav.metronomeMenuOpen
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                onClosed: Nav.metronomeMenuOpen = false
            }
        }
    }
}
