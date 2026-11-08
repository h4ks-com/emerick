import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui
import Emerick.Broadcast

// Beside the transport counter while a broadcast is live, retrying or refused; a click shows the broadcasts.
AbstractButton {
    id: badge
    readonly property bool shown: Broadcasts.badge !== ""
    readonly property color tint: Broadcasts.badge === "retrying" ? Theme.tape : Theme.rec
    objectName: "broadcast.show"
    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    onClicked: Registry.activate("broadcast.show")
    ToolTip.text: Broadcasts.badge === "live" ? "Show broadcasts"
                  : Broadcasts.badge === "error" ? "Show broadcasts, a server refused one"
                  : "Show broadcasts, one is reconnecting"
    ToolTip.visible: hovered
    ToolTip.delay: Theme.tooltipDelay
    contentItem: Row {
        id: row
        spacing: Theme.space1
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.space2
            height: Theme.space2
            radius: width / 2
            color: badge.tint
        }
        Label {
            text: Broadcasts.badge === "error" ? "Error" : "Live"
            color: badge.tint
            font.pixelSize: Theme.textSmall
        }
    }
}
