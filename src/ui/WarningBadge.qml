import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A warning icon that shows only while message is set, with the whole message in its tooltip.
ToolButton {
    id: badge
    property string message

    visible: message !== ""
    hoverEnabled: true
    padding: 0
    implicitWidth: Theme.buttonHeight
    implicitHeight: Theme.buttonHeight
    display: AbstractButton.IconOnly
    icon.source: Theme.iconSource("warning")
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: Theme.rec
    background: null
    Accessible.name: message

    ToolTip {
        id: tip
        objectName: badge.objectName + ":tip"
        parent: badge
        text: badge.message
        visible: badge.hovered || badge.pressed
        delay: badge.pressed ? 0 : Theme.tooltipDelay
        width: Math.min(implicitWidth, Theme.tipWidth)
        contentItem: Text {
            text: tip.text
            font: tip.font
            color: tip.palette.toolTipText
            wrapMode: Text.Wrap
        }
    }
}
