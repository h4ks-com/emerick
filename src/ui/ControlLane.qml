import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One controller's values as a line that holds each value until the next point. With the pencil, or with Command held, a drag draws values; otherwise a drag on
// a point moves it, a drag on empty space selects a range for Delete, and Alt-click removes a point.
Item {
    id: values

    required property PianoRoll roll
    // The lane as Tracks.laneChoices lists it.
    required property var choice

    ControlCurve {
        id: curve
        anchors.fill: parent
        points: values.roll.hasClip && Tracks.revision >= 0 ? Tracks.controlPoints(values.roll.clipId, values.choice.id) : []
        minimum: values.choice.minimum
        maximum: values.choice.maximum
        switched: values.choice.switched
        originTicks: values.roll.scrollTicks - values.roll.clipStart
        pixelsPerTick: values.roll.pixelsPerTick
        length: values.roll.clipLength
        color: Theme.trackColors[(values.roll.clip.colorIndex ?? 0) % Theme.trackColors.length]
        hovered: edit.containsMouse && (edit.mode === "" || edit.mode === "move")
                 ? indexNear(edit.mouseX, Theme.controlPointSlack) : -1
    }

    Rectangle {
        visible: values.choice.minimum < 0
        y: curve.yOf(0)
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }

    Rectangle {
        readonly property var range: values.roll.controlRange
        visible: range !== null && range.lane === values.choice.id
        x: visible ? values.roll.xOf(range.from) : 0
        width: visible ? Math.max(Theme.hairline, values.roll.xOf(range.to) - x) : 0
        height: parent.height
        color: Theme.bandFill
        border.width: Theme.hairline
        border.color: Theme.tape
    }

    MouseArea {
        id: edit
        objectName: "midi.setControl:" + values.choice.id
        anchors.fill: parent
        enabled: values.roll.hasClip && !Registry.readOnly
        hoverEnabled: true
        property string mode: ""
        property point pressAt
        property real pressTick
        property int pressValue
        // The pencil stroke keeps one point per pixel column.
        property var stroke: ({})
        property real lastX
        property string tip: ""
        ToolTip.visible: mode !== "" && mode !== "band" && tip !== ""
        ToolTip.text: tip

        function tickAt(x) {
            return Math.max(0, Math.min(values.roll.clipLength - 1, Math.round(values.roll.ticksAt(x))))
        }
        function valueAt(y) {
            const value = curve.valueAt(y)
            return values.choice.switched ? (value >= 64 ? 127 : 0) : value
        }
        function run(params) {
            Registry.updateGesture(Registry.actionInfo("midi.setControl").title,
                                   [{id: "midi.setControl", params: Object.assign({clip: values.roll.clipId, lane: values.choice.id}, params)}])
        }
        function draw(x, y) {
            const step = x >= lastX ? 1 : -1
            const value = valueAt(y)
            for (let column = Math.round(lastX); column !== Math.round(x) + step; column += step) {
                stroke[column] = {at: tickAt(column), value: value}
            }
            lastX = x
            const points = Object.values(stroke).sort((a, b) => a.at - b.at)
            tip = "" + value
            run({from: points[0].at, to: points[points.length - 1].at, points: points})
        }

        onPressed: mouse => {
            values.roll.forceActiveFocus()
            values.roll.controlRange = null
            pressAt = Qt.point(mouse.x, mouse.y)
            if (Registry.settings["midi.pencil"] === true || (mouse.modifiers & Qt.ControlModifier)) {
                mode = "draw"
                stroke = {}
                lastX = mouse.x
                draw(mouse.x, mouse.y)
                return
            }
            const index = curve.indexNear(mouse.x, Theme.controlPointSlack)
            if (index >= 0 && (mouse.modifiers & Qt.AltModifier)) {
                Registry.runAction("midi.setControl", {clip: values.roll.clipId, lane: values.choice.id,
                                                       from: curve.atOf(index), to: curve.atOf(index)})
                return
            }
            if (index >= 0) {
                mode = "move"
                pressTick = curve.atOf(index)
                pressValue = curve.valueOf(index)
                tip = "" + pressValue
                return
            }
            mode = "band"
        }
        onPositionChanged: mouse => {
            if (!pressed || mode === "") {
                return
            }
            if (mode === "draw") {
                draw(mouse.x, mouse.y)
                return
            }
            if (mode === "band") {
                const from = tickAt(Math.min(pressAt.x, mouse.x))
                const to = tickAt(Math.max(pressAt.x, mouse.x))
                values.roll.controlRange = Math.abs(mouse.x - pressAt.x) < Theme.dragThreshold ? null
                                                                                               : {lane: values.choice.id, from: from, to: to}
                return
            }
            if (Math.abs(mouse.x - pressAt.x) < Theme.dragThreshold && Math.abs(mouse.y - pressAt.y) < Theme.dragThreshold) {
                return
            }
            const at = tickAt(curve.xOf(pressTick) + mouse.x - pressAt.x)
            const value = valueAt(curve.yOf(pressValue) + mouse.y - pressAt.y)
            tip = "" + value
            run({from: pressTick, to: pressTick, points: [{at: at, value: value}]})
        }
        onReleased: {
            if (mode === "draw" || mode === "move") {
                Registry.endGesture()
            }
            mode = ""
        }
        onCanceled: {
            if (mode === "draw" || mode === "move") {
                Registry.cancelGesture()
                Registry.endGesture()
            }
            mode = ""
        }
    }
}
