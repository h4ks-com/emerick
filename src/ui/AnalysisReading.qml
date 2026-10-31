import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One level from a track analysis with its unit, explained on hover.
Row {
    id: reading
    required property var value
    required property string unit
    required property string tip
    readonly property alias text: number.text
    spacing: Theme.space1

    function level(value) {
        return value === null || value === undefined ? "-inf" : value.toFixed(1)
    }

    Label {
        id: number
        text: reading.level(reading.value)
        color: Theme.ink
        font.pixelSize: Theme.textSmall
        font.features: {"tnum": 1}
    }
    Label {
        text: reading.unit
        color: Theme.inkDim
        font.pixelSize: Theme.textSmall
    }
    HoverHandler { id: hover }
    ToolTip.text: tip
    ToolTip.visible: hover.hovered
    ToolTip.delay: Theme.tooltipDelay
}
