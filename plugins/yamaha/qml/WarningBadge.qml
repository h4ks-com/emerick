import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.Ui

// A red warning icon that shows message in its tooltip and hides while message is empty.
AbstractButton {
    id: badge
    property string message

    visible: message !== ""
    implicitWidth: Theme.headerButton
    implicitHeight: Theme.headerButton
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    display: AbstractButton.IconOnly
    icon.source: "warning.svg"
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: Theme.rec
    contentItem: IconLabel {
        icon: badge.icon
        display: badge.display
    }
    background: null
    ToolTip.text: message
    ToolTip.visible: hovered
    ToolTip.delay: Theme.tooltipDelay
}
