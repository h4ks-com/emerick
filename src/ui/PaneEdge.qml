import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The right edge of a pane whose width is a registry setting. Dragging it sets the setting live, and the setting
// keeps the width within its limits.
MouseArea {
    id: edge
    required property string settingId
    property real pressX
    property int pressWidth

    objectName: settingId
    width: Theme.splitterGrip
    z: 1
    hoverEnabled: true
    cursorShape: Qt.SizeHorCursor
    preventStealing: true
    onPressed: mouse => {
        pressX = mapToItem(null, mouse.x, 0).x
        pressWidth = Registry.settings[settingId]
    }
    onPositionChanged: mouse => {
        if (pressed) {
            Registry.setSettingValue(settingId, pressWidth + mapToItem(null, mouse.x, 0).x - pressX)
        }
    }

    ToolTip.text: "Drag to resize"
    ToolTip.visible: containsMouse && !pressed
    ToolTip.delay: Theme.tooltipDelay
}
