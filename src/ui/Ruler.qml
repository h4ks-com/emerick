import QtQuick
import QtQuick.Controls.Basic
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

    readonly property var cycle: Project.cycle
    readonly property real cycleHeight: height / 2
    readonly property real shortestCycle: Project.ticksPerBeat / 4

    color: Theme.slate
    clip: true

    function ticksAt(x) {
        return Math.max(0, Math.round(scrollTicks + x / pixelsPerTick))
    }
    function xOf(ticks) {
        return (ticks - scrollTicks) * pixelsPerTick
    }
    function dragCycle(params) {
        Registry.updateGesture(Registry.actionInfo("cycle.set").title, [{id: "cycle.set", params: params}])
    }

    Rectangle {
        id: cycleBar
        visible: ruler.cycle.end > ruler.cycle.start
        x: ruler.xOf(ruler.cycle.start)
        width: Math.max(Theme.hairline, (ruler.cycle.end - ruler.cycle.start) * ruler.pixelsPerTick)
        height: ruler.cycleHeight
        radius: Theme.radiusClip / 2
        color: ruler.cycle.enabled ? Theme.tape : Theme.inkDim
        opacity: ruler.cycle.enabled ? 0.45 : 0.25
    }

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
                x: Theme.space2
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

    // A drag along the upper half sets new locators, as in Logic.
    MouseArea {
        id: cycleArea
        objectName: "cycle.set"
        visible: !Registry.readOnly
        width: parent.width
        height: ruler.cycleHeight
        hoverEnabled: true
        property real pressTicks
        property real pressX
        property bool dragging: false
        ToolTip.text: "Drag to set the cycle locators"
        ToolTip.visible: containsMouse && !pressed && !cycleBody.containsMouse && !startEdge.containsMouse && !endEdge.containsMouse
        ToolTip.delay: Theme.tooltipDelay
        onPressed: mouse => {
            pressTicks = Project.placed(ruler.ticksAt(mouse.x), mouse.modifiers)
            pressX = mouse.x
            dragging = false
        }
        onPositionChanged: mouse => {
            if (!pressed) {
                return
            }
            const at = Project.placed(ruler.ticksAt(mouse.x), mouse.modifiers)
            dragging = dragging || Math.abs(mouse.x - pressX) >= Theme.dragThreshold
            if (dragging && Math.abs(at - pressTicks) >= ruler.shortestCycle) {
                ruler.dragCycle({start: Math.min(at, pressTicks), end: Math.max(at, pressTicks), enabled: true})
            }
        }
        onReleased: Registry.endGesture()
        onCanceled: {
            Registry.cancelGesture()
            Registry.endGesture()
        }
    }

    // A click on the bar turns the cycle on or off, and a drag moves it.
    MouseArea {
        id: cycleBody
        objectName: "cycle.toggle"
        visible: cycleBar.visible && !Registry.readOnly
        x: cycleBar.x
        width: cycleBar.width
        height: ruler.cycleHeight
        hoverEnabled: true
        cursorShape: pressed && dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real pressX
        property real pressStart
        property real length
        property bool dragging: false
        readonly property var info: Registry.actionInfo("cycle.toggle")
        ToolTip.text: info.title + (info.shortcut ? "   " + Theme.shortcutText(info.shortcut) : "") + "\nDrag to move the cycle"
        ToolTip.visible: containsMouse && !pressed && !startEdge.containsMouse && !endEdge.containsMouse
        ToolTip.delay: Theme.tooltipDelay
        onPressed: mouse => {
            pressX = mapToItem(ruler, mouse.x, 0).x
            pressStart = ruler.cycle.start
            length = ruler.cycle.end - ruler.cycle.start
            dragging = false
        }
        onPositionChanged: mouse => {
            if (!pressed) {
                return
            }
            const dx = mapToItem(ruler, mouse.x, 0).x - pressX
            dragging = dragging || Math.abs(dx) >= Theme.dragThreshold
            if (dragging) {
                const start = Math.max(0, Project.placed(Math.round(pressStart + dx / ruler.pixelsPerTick), mouse.modifiers))
                ruler.dragCycle({start: start, end: start + length})
            }
        }
        onReleased: {
            if (dragging) {
                Registry.endGesture()
            } else {
                Registry.runAction("cycle.toggle", {})
            }
        }
        onCanceled: {
            Registry.cancelGesture()
            Registry.endGesture()
        }
    }

    component CycleEdge: MouseArea {
        id: edge
        required property bool isStart
        visible: cycleBar.visible && !Registry.readOnly
        width: Theme.space2
        height: ruler.cycleHeight
        x: (isStart ? cycleBar.x : cycleBar.x + cycleBar.width) - width / 2
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        ToolTip.text: isStart ? "Drag to move the left locator" : "Drag to move the right locator"
        ToolTip.visible: containsMouse && !pressed
        ToolTip.delay: Theme.tooltipDelay
        onPositionChanged: mouse => {
            if (!pressed) {
                return
            }
            const at = Project.placed(ruler.ticksAt(mapToItem(ruler, mouse.x, 0).x), mouse.modifiers)
            if (isStart ? at <= ruler.cycle.end - ruler.shortestCycle : at >= ruler.cycle.start + ruler.shortestCycle) {
                ruler.dragCycle(isStart ? {start: at} : {end: at})
            }
        }
        onReleased: Registry.endGesture()
        onCanceled: {
            Registry.cancelGesture()
            Registry.endGesture()
        }
    }
    CycleEdge {
        id: startEdge
        objectName: "cycle.set:start"
        isStart: true
    }
    CycleEdge {
        id: endEdge
        objectName: "cycle.set:end"
        isStart: false
    }

    MouseArea {
        objectName: "transport.locate"
        y: Registry.readOnly ? 0 : ruler.cycleHeight
        width: parent.width
        height: parent.height - y
        function locate(mouse) {
            const ticks = Math.max(0, Math.round(ruler.scrollTicks + Math.min(Math.max(mouse.x, 0), width) / ruler.pixelsPerTick))
            Registry.runAction("transport.locate", {at: Project.placed(ticks, mouse.modifiers)})
        }
        onPressed: mouse => locate(mouse)
        onPositionChanged: mouse => locate(mouse)
    }
}
