import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One row of a glass menu: the title, the shortcut it shows and the arrow of a submenu.
MenuItem {
    id: row
    property string shortcut

    implicitHeight: Theme.buttonHeight
    contentItem: Item {
        implicitWidth: label.implicitWidth + keys.implicitWidth + Theme.space5
        implicitHeight: label.implicitHeight
        Text {
            id: label
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: row.text
            color: row.enabled ? Theme.ink : Theme.inkDim
            font.pixelSize: Theme.textBody
        }
        Text {
            id: keys
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: row.shortcut ? Theme.shortcutText(row.shortcut) : ""
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
        }
    }
    arrow: Text {
        x: row.width - width - Theme.space2
        y: (row.height - height) / 2
        visible: row.subMenu !== null
        text: "›"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
    background: Rectangle {
        radius: Theme.radiusClip
        color: row.highlighted ? Theme.lift : Theme.clear
    }
}
