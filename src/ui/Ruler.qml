import QtQuick
import Emerick.App

Rectangle {
    id: ruler

    property real scrollTicks
    property real pixelsPerTick
    // Bars between two labels, a power of two that keeps the labels apart at this zoom.
    property int barStep: 1
    property bool showBeats: false

    readonly property real stepTicks: barStep * Project.ticksPerBar
    readonly property int firstStep: Math.floor(scrollTicks / stepTicks)

    color: Theme.slate
    clip: true

    Repeater {
        model: Math.ceil(ruler.width / (ruler.stepTicks * ruler.pixelsPerTick)) + 1
        delegate: Item {
            id: mark
            required property int index
            readonly property int bar: (ruler.firstStep + index) * ruler.barStep
            x: (bar * Project.ticksPerBar - ruler.scrollTicks) * ruler.pixelsPerTick
            width: ruler.stepTicks * ruler.pixelsPerTick
            height: ruler.height

            Rectangle {
                y: parent.height / 2
                width: Theme.hairline
                height: parent.height / 2
                color: Theme.inkDim
            }
            Text {
                x: Theme.space1
                y: Theme.space1
                text: mark.bar + 1
                color: Theme.inkDim
                font.pixelSize: Theme.textSmall
                font.features: {"tnum": 1}
            }
            Repeater {
                model: ruler.showBeats ? Project.ticksPerBar / Project.ticksPerBeat - 1 : 0
                delegate: Rectangle {
                    required property int index
                    x: (index + 1) * Project.ticksPerBeat * ruler.pixelsPerTick
                    y: mark.height - height
                    width: Theme.hairline
                    height: mark.height / 4
                    color: Theme.rule
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }

    MouseArea {
        objectName: "transport.locate"
        anchors.fill: parent
        function locate(mouse) {
            const ticks = Math.max(0, Math.round(ruler.scrollTicks + Math.min(Math.max(mouse.x, 0), width) / ruler.pixelsPerTick))
            Registry.runAction("transport.locate", {position: Project.placed(ticks, mouse.modifiers)})
        }
        onPressed: mouse => locate(mouse)
        onPositionChanged: mouse => locate(mouse)
    }
}
