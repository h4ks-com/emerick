import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Button {
    id: button
    property string actionId: objectName
    property var params
    property bool active: false
    property color activeColor: Theme.tape
    readonly property var info: Registry.actionInfo(actionId)
    readonly property string shortcutText: info.shortcut ? Theme.shortcutText(info.shortcut) : ""

    text: info.title
    onClicked: params === undefined ? Registry.activate(actionId) : Registry.runAction(actionId, params)
    padding: Theme.space2
    ToolTip.text: shortcutText ? info.title + "   " + shortcutText : info.title
    ToolTip.visible: hovered
    contentItem: Text {
        text: button.text
        color: button.active ? Theme.graphite : Theme.ink
        font.pixelSize: Theme.textLabel
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitHeight: Theme.buttonHeight
        color: button.active ? button.activeColor : button.down || button.hovered ? Theme.rule : Theme.lift
        radius: Theme.radiusClip
    }
}
