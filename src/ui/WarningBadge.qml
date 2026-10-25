import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.App

// A warning icon that shows its message in a tooltip on hover.
IconImage {
    id: badge
    property string message

    // A plugin's QML would resolve the relative icon path against its own module, so we resolve it here.
    source: Qt.resolvedUrl(Theme.iconSource("warning-octagon"))
    sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
    color: Theme.rec
    HoverHandler {
        id: hover
    }
    ToolTip.text: badge.message
    ToolTip.visible: hover.hovered && badge.message !== ""
    ToolTip.delay: Theme.tooltipDelay
}
