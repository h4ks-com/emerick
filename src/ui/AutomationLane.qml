import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Emerick.App

// A track's automation lane over its clips, as a line with a handle per point in the track color. Click to add a point,
// drag points or a segment to move them, drag empty space to select, and Alt-click a point to delete it.
Item {
    id: lane
    required property string trackId
    required property color trackColor
    // The parameter as the track list describes it: {id, title, minimum, maximum, value, toggle, unit, scale}.
    required property var param
    required property var points
    required property real scrollTicks
    required property real pixelsPerTick
    property bool reads: true

    objectName: "automation.lane:" + trackId
    readonly property real inset: Theme.fadeHandle
    readonly property real span: param.maximum - param.minimum

    function xOf(tick) {
        return (tick - scrollTicks) * pixelsPerTick
    }
    function ticksAt(x) {
        return scrollTicks + x / pixelsPerTick
    }
    // A fader taper spreads gain as the square root of its amplitude, so the few dB around 0 get room to show.
    function faderLevel(db) {
        return Math.pow(10, db / 40)
    }
    function positionOf(value) {
        const clamped = Math.max(param.minimum, Math.min(param.maximum, value))
        if (param.scale === "logarithmic") {
            return Math.log(clamped / param.minimum) / Math.log(param.maximum / param.minimum)
        }
        if (param.scale === "fader") {
            const low = faderLevel(param.minimum)
            return (faderLevel(clamped) - low) / (faderLevel(param.maximum) - low)
        }
        return (clamped - param.minimum) / span
    }
    function valueAtPosition(position) {
        if (param.scale === "logarithmic") {
            return param.minimum * Math.pow(param.maximum / param.minimum, position)
        }
        if (param.scale === "fader") {
            const low = faderLevel(param.minimum)
            return 40 * Math.log10(Math.max(Number.MIN_VALUE, low + position * (faderLevel(param.maximum) - low)))
        }
        return param.minimum + position * span
    }
    function yOf(value) {
        return inset + (1 - positionOf(value)) * (height - 2 * inset)
    }
    function valueAt(y) {
        const position = Math.max(0, Math.min(1, 1 - (y - inset) / (height - 2 * inset)))
        const value = valueAtPosition(position)
        return Math.max(param.minimum, Math.min(param.maximum, param.toggle ? Math.round(value) : value))
    }
    function valueText(value) {
        if (param.toggle) {
            return value >= 0.5 ? "On" : "Off"
        }
        if (param.unit === "dB") {
            return value.toFixed(1) + " dB"
        }
        if (param.unit === "pan") {
            const amount = Math.round(Math.abs(value) * 100)
            return amount === 0 ? "C" : (value < 0 ? "L " : "R ") + amount
        }
        return value.toFixed(2)
    }
    function selected(id) {
        return Nav.selectedPoints.indexOf(id) >= 0
    }
    function pointAt(x, y) {
        for (let index = points.length - 1; index >= 0; --index) {
            const point = points[index]
            if (Math.abs(xOf(point.tick) - x) <= Theme.fadeHandle && Math.abs(yOf(point.value) - y) <= Theme.fadeHandle) {
                return point
            }
        }
        return null
    }
    // The two points around x when the line between them passes near y.
    function segmentAt(x, y) {
        const tick = ticksAt(x)
        for (let index = 0; index + 1 < points.length; ++index) {
            const from = points[index]
            const to = points[index + 1]
            if (tick < from.tick || tick > to.tick || to.tick === from.tick) {
                continue
            }
            const t = (tick - from.tick) / (to.tick - from.tick)
            const value = from.curve === "hold" ? from.value
                        : from.value + (to.value - from.value) * (from.curve === "smooth" ? t * t * (3 - 2 * t) : t)
            return Math.abs(yOf(value) - y) <= Theme.dragThreshold ? [from, to] : null
        }
        return null
    }

    // The line through the points from the left edge to the right edge, flat at the parameter's value without points.
    function linePath() {
        if (points.length === 0) {
            return [Qt.point(0, yOf(param.value)), Qt.point(width, yOf(param.value))]
        }
        const path = [Qt.point(0, yOf(points[0].value))]
        for (let index = 0; index < points.length; ++index) {
            const point = points[index]
            const next = points[index + 1]
            path.push(Qt.point(xOf(point.tick), yOf(point.value)))
            if (next === undefined) {
                break
            }
            if (point.curve === "hold") {
                path.push(Qt.point(xOf(next.tick), yOf(point.value)))
            } else if (point.curve === "smooth" || param.scale !== "linear") {
                const steps = 16
                for (let step = 1; step < steps; ++step) {
                    const t = step / steps
                    const eased = point.curve === "smooth" ? t * t * (3 - 2 * t) : t
                    path.push(Qt.point(xOf(point.tick + (next.tick - point.tick) * t),
                                       yOf(point.value + (next.value - point.value) * eased)))
                }
            }
        }
        path.push(Qt.point(width, yOf(points[points.length - 1].value)))
        return path
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.fadeShade
    }

    // We draw the line with a Shape, because a Canvas paints a frame late and video frames would miss it.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: Theme.selectionWidth
            strokeColor: lane.points.length > 0 && lane.reads ? lane.trackColor : Theme.inkDim
            fillColor: "transparent"
            PathPolyline {
                path: lane.linePath()
            }
        }
    }

    Repeater {
        model: lane.points
        delegate: Rectangle {
            required property var modelData
            objectName: "automation.point:" + modelData.id
            readonly property bool picked: lane.selected(modelData.id)
            x: lane.xOf(modelData.tick) - width / 2
            y: lane.yOf(modelData.value) - height / 2
            width: Theme.fadeHandle
            height: Theme.fadeHandle
            radius: width / 2
            visible: x + width >= 0 && x <= lane.width
            color: picked ? Theme.tape : lane.trackColor
            border.width: Theme.hairline
            border.color: picked ? Theme.ink : Theme.graphite
        }
    }

    Rectangle {
        id: band
        visible: false
        color: Theme.bandFill
        border.width: Theme.hairline
        border.color: Theme.tape
    }

    MouseArea {
        id: edit
        objectName: "automation.addPoint:" + lane.trackId
        anchors.fill: parent
        acceptedButtons: Registry.readOnly ? Qt.NoButton : Qt.LeftButton
        hoverEnabled: true
        readonly property var hovered: containsMouse ? lane.pointAt(mouseX, mouseY) : null
        ToolTip.visible: containsMouse && (hovered !== null || !Registry.readOnly)
        ToolTip.delay: hovered || pressed ? 0 : Theme.tooltipDelay
        ToolTip.text: hovered ? lane.param.title + "   " + lane.valueText(hovered.value) + "   "
                                + Project.ticksText(hovered.tick, false)
                              : "Click to add a point, drag to select, Alt-click a point to delete it"
        property string mode: ""
        property point pressAt
        property var pressPoint: null
        property var dragged: []
        property var pressSelection: []

        function select(point, modifiers) {
            if (modifiers & Qt.ShiftModifier) {
                const ids = Array.from(Nav.selectedPoints)
                const at = ids.indexOf(point.id)
                if (at >= 0) {
                    ids.splice(at, 1)
                } else {
                    ids.push(point.id)
                }
                Registry.runAction("automation.selectPoints", {points: ids})
            } else if (!lane.selected(point.id)) {
                Registry.runAction("automation.selectPoints", {points: [point.id]})
            }
        }

        onPressed: mouse => {
            pressAt = Qt.point(mouse.x, mouse.y)
            pressPoint = lane.pointAt(mouse.x, mouse.y)
            if (pressPoint && (mouse.modifiers & Qt.AltModifier)) {
                Registry.runAction("automation.deletePoints", {points: [pressPoint.id]})
                mode = ""
                return
            }
            if (pressPoint) {
                select(pressPoint, mouse.modifiers)
                dragged = Array.from(Nav.selectedPoints)
                mode = "move"
                return
            }
            const segment = lane.segmentAt(mouse.x, mouse.y)
            if (segment) {
                pressPoint = segment[0]
                dragged = segment.map(point => point.id)
                mode = "segment"
                return
            }
            pressSelection = mouse.modifiers & Qt.ShiftModifier ? Array.from(Nav.selectedPoints) : []
            mode = "click"
        }
        onPositionChanged: mouse => {
            if (!pressed || mode === "") {
                return
            }
            const dx = mouse.x - pressAt.x
            const dy = mouse.y - pressAt.y
            if (Math.abs(dx) < Theme.dragThreshold && Math.abs(dy) < Theme.dragThreshold && mode !== "band") {
                return
            }
            if (mode === "click" || mode === "band") {
                mode = "band"
                band.x = Math.min(pressAt.x, mouse.x)
                band.y = Math.min(pressAt.y, mouse.y)
                band.width = Math.abs(dx)
                band.height = Math.abs(dy)
                band.visible = true
                return
            }
            const by = mode === "move"
                ? Project.placed(Math.round(pressPoint.tick + dx / lane.pixelsPerTick), mouse.modifiers) - pressPoint.tick
                : 0
            const delta = lane.valueAt(lane.yOf(pressPoint.value) + dy) - pressPoint.value
            Registry.updateGesture(Registry.actionInfo("automation.movePoints").title,
                                   [{id: "automation.movePoints", params: {points: dragged, by: by, delta: delta}}])
        }
        onReleased: mouse => {
            if (mode === "click") {
                Registry.runAction("automation.addPoint", {
                    track: lane.trackId,
                    param: lane.param.id,
                    tick: Project.placed(Math.max(0, Math.round(lane.ticksAt(mouse.x))), mouse.modifiers),
                    value: lane.valueAt(mouse.y)
                })
            } else if (mode === "band") {
                const from = lane.ticksAt(band.x)
                const to = lane.ticksAt(band.x + band.width)
                const inside = lane.points.filter(point => point.tick >= from && point.tick <= to
                                                  && lane.yOf(point.value) >= band.y
                                                  && lane.yOf(point.value) <= band.y + band.height)
                const ids = pressSelection.concat(inside.map(point => point.id).filter(id => pressSelection.indexOf(id) < 0))
                Registry.runAction("automation.selectPoints", {points: ids})
                band.visible = false
            } else if (mode !== "") {
                Registry.endGesture()
            }
            mode = ""
        }
        onCanceled: {
            band.visible = false
            if (mode === "move" || mode === "segment") {
                Registry.cancelGesture()
                Registry.endGesture()
            }
            mode = ""
        }
    }
}
