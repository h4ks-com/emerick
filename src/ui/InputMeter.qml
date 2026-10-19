import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The input level of one audio track in dBFS, red from the first full scale sample until the next recording.
Rectangle {
    id: meter
    required property string trackId
    readonly property var level: Project.inputLevels[trackId]
    readonly property real peakDb: level && level.peak > 0 ? 20 * Math.log10(level.peak) : Theme.meterFloorDb
    readonly property bool clipped: level !== undefined && level.clipped

    visible: level !== undefined
    implicitHeight: Theme.hairline * 3
    radius: height / 2
    color: Theme.graphite

    Rectangle {
        width: parent.width * Math.max(0, 1 - meter.peakDb / Theme.meterFloorDb)
        height: parent.height
        radius: parent.radius
        color: meter.clipped ? Theme.rec : Theme.inkDim
    }
    HoverHandler {
        id: hover
    }
    ToolTip.visible: meter.clipped && hover.hovered
    ToolTip.text: "Turn down the source's output level, the input clipped before Emerick"
}
