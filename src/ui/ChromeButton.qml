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
    property int textSize: Theme.textLabel
    // A button with a menu opens it below itself, and the menu's items run the action.
    property Menu menu
    // We call run on click, for an action the UI runs off its thread.
    property var run
    // A delegate binds its objectName after this first evaluates, while actionId is still empty.
    readonly property var info: actionId ? Registry.actionInfo(actionId) : ({})
    readonly property string shortcutText: info.shortcut ? Theme.shortcutText(info.shortcut) : ""

    text: info.title
    onClicked: {
        if (menu) {
            menu.popup(button, 0, height)
        } else if (run) {
            run()
        } else if (params === undefined) {
            Registry.activate(actionId)
        } else {
            Registry.runAction(actionId, params)
        }
    }
    padding: iconName ? 0 : Theme.space2
    display: iconName ? AbstractButton.IconOnly : AbstractButton.TextOnly
    // A plugin's QML would resolve the relative icon path against its own module, so we resolve it here.
    icon.source: iconName ? Qt.resolvedUrl(Theme.iconSource(iconName)) : ""
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: active ? activeColor : iconColor
    ToolTip.text: (shortcutText ? info.title + "   " + shortcutText : info.title) + (hint ? "\n" + hint : "")
    // A press dismisses the tooltip until the pointer leaves, so it never covers the menu or panel the button opens.
    property bool tipDismissed: false
    onPressed: tipDismissed = true
    // A disabled button gets no hover, and a read-only view still names what each button does.
    HoverHandler {
        id: pointer
        onHoveredChanged: if (!hovered) button.tipDismissed = false
    }
    ToolTip.visible: pointer.hovered && !tipDismissed && opacity > 0 && ToolTip.text !== ""
    ToolTip.delay: Theme.tooltipDelay
    contentItem: IconLabel {
        icon: button.icon
        text: button.text
        display: button.display
        color: button.active ? Theme.graphite : Theme.ink
        font.pixelSize: button.textSize
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
