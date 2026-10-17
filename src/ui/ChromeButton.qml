import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.App

Button {
    id: button
    property string actionId: objectName
    property var params
    property bool active: false
    property color activeColor: Theme.tape
    property string iconName
    property color iconColor: Theme.ink
    readonly property var info: Registry.actionInfo(actionId)
    readonly property string shortcutText: info.shortcut ? Theme.shortcutText(info.shortcut) : ""

    text: info.title
    onClicked: params === undefined ? Registry.activate(actionId) : Registry.runAction(actionId, params)
    padding: iconName ? 0 : Theme.space2
    display: iconName ? AbstractButton.IconOnly : AbstractButton.TextOnly
    icon.source: iconName ? Theme.iconSource(iconName) : ""
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: active ? activeColor : iconColor
    ToolTip.text: shortcutText ? info.title + "   " + shortcutText : info.title
    ToolTip.visible: hovered
    ToolTip.delay: Theme.tooltipDelay
    contentItem: IconLabel {
        icon: button.icon
        text: button.text
        display: button.display
        color: button.active ? Theme.graphite : Theme.ink
        font.pixelSize: Theme.textLabel
    }
    background: Rectangle {
        implicitWidth: button.iconName ? Theme.buttonHeight : 0
        implicitHeight: Theme.buttonHeight
        radius: Theme.radiusClip
        color: button.iconName ? (button.active ? Theme.glow(button.activeColor) : button.down ? Theme.rule : button.hovered ? Theme.lift : Theme.clear) : button.active ? button.activeColor : button.down || button.hovered ? Theme.rule : Theme.lift
    }
}
