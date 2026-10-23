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
    // A second tooltip line, such as a modifier that changes what a drag does.
    property string hint
    // A button with a menu opens it below itself, and the menu's items run the action.
    property Menu menu
    // A delegate binds its objectName after this first evaluates, while actionId is still empty.
    readonly property var info: actionId ? Registry.actionInfo(actionId) : ({})
    readonly property string shortcutText: info.shortcut ? Theme.shortcutText(info.shortcut) : ""

    text: info.title
    onClicked: {
        if (menu) {
            menu.popup(button, 0, height)
        } else if (params === undefined) {
            Registry.activate(actionId)
        } else {
            Registry.runAction(actionId, params)
        }
    }
    padding: iconName ? 0 : Theme.space2
    display: iconName ? AbstractButton.IconOnly : AbstractButton.TextOnly
    icon.source: iconName ? Theme.iconSource(iconName) : ""
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: active ? activeColor : iconColor
    ToolTip.text: (shortcutText ? info.title + "   " + shortcutText : info.title) + (hint ? "\n" + hint : "")
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
        color: button.active ? (button.iconName ? Theme.glow(button.activeColor) : button.activeColor)
                             : button.down ? Theme.buttonDown
                             : button.hovered ? Theme.buttonHover
                             : button.iconName ? Theme.clear : Theme.buttonFill
    }
}
