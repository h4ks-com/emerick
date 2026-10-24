import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.App

// One row of a glass menu: an optional icon or color swatch, the title, the shortcut it shows and the arrow of a
// submenu.
MenuItem {
    id: row
    property string shortcut
    // A picker's menu marks its current choice with a check.
    property bool marked: false
    property string iconName
    // A track color index, or -1 for none.
    property int swatch: -1
    readonly property bool decorated: iconName !== "" || swatch >= 0

    implicitHeight: Theme.buttonHeight
    hoverEnabled: true
    leftPadding: menu && menu.marks ? Theme.space2 + Theme.space4 : undefined
    indicator: Text {
        x: Theme.space2
        y: (row.height - height) / 2
        visible: row.marked
        text: "✓"
        color: Theme.ink
        font.pixelSize: Theme.textLabel
    }
    contentItem: Item {
        implicitWidth: (row.decorated ? Theme.iconSize + Theme.space2 : 0) + label.implicitWidth + keys.implicitWidth
                       + Theme.space5
        implicitHeight: label.implicitHeight
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            visible: row.iconName !== ""
            source: row.iconName ? Theme.iconSource(row.iconName) : ""
            sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
            color: row.enabled ? Theme.ink : Theme.inkDim
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: row.swatch >= 0
            width: Theme.iconSize
            height: Theme.iconSize
            radius: Theme.radiusClip
            color: row.swatch >= 0 ? Theme.trackColors[row.swatch % Theme.trackColors.length] : Theme.clear
        }
        Text {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: row.decorated ? Theme.iconSize + Theme.space2 : 0
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
