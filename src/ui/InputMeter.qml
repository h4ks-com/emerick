import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The input level of one audio track in dBFS, red from the first full scale sample until the next recording.
Rectangle {
    id: meter
    required property string trackId
    property bool vertical: false
    readonly property var level: Project.inputLevels[trackId]
    readonly property real peakDb: level && level.peak > 0 ? 20 * Math.log10(level.peak) : Theme.meterFloorDb
    readonly property real fill: Math.max(0, 1 - peakDb / Theme.meterFloorDb)
    readonly property bool clipped: level !== undefined && level.clipped

    visible: level !== undefined
    implicitWidth: vertical ? Theme.faderGroove : 0
    implicitHeight: vertical ? 0 : Theme.faderGroove
    radius: Math.min(width, height) / 2
    color: Theme.graphite

    Rectangle {
        y: meter.vertical ? parent.height * (1 - meter.fill) : 0
        width: meter.vertical ? parent.width : parent.width * meter.fill
        height: meter.vertical ? parent.height * meter.fill : parent.height
        radius: parent.radius
        color: meter.clipped ? Theme.rec : Theme.inkDim
    }
    HoverHandler {
        id: hover
    }
    ToolTip.visible: meter.clipped && hover.hovered
    ToolTip.text: "Turn down the source's output level, the input clipped before Emerick"
}
