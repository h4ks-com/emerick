import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The Channel EQ's response from 20 Hz to 20 kHz with a point per band, after Logic's: drag a point to move its band's
// frequency and gain, use the wheel over it to change its Q and double-click it to turn the band on or off. A drag
// runs live as one undo step.
Item {
    id: graph
    required property var insert
    readonly property var params: Effects.params("eq")
    readonly property var curve: Effects.eqCurve(insert.params, Math.max(2, Math.round(width / Theme.eqCurveSpacing)))
    readonly property var frequencies: [50, 100, 200, 500, 1000, 2000, 5000, 10000]
    readonly property var levels: [-18, -12, -6, 0, 6, 12, 18]

    function valueOf(index) {
        const param = params[index]
        return insert.params[param.id] ?? param.defaultValue
    }
    function xOf(frequency) {
        return Math.log(frequency / Theme.eqMinHz) / Math.log(Theme.eqMaxHz / Theme.eqMinHz) * width
    }
    function frequencyAt(x) {
        const at = Math.min(1, Math.max(0, x / width))
        return Number((Theme.eqMinHz * Math.pow(Theme.eqMaxHz / Theme.eqMinHz, at)).toPrecision(3))
    }
    function yOf(db) {
        return height / 2 - db / Theme.eqRangeDb * height / 2
    }
    function dbAt(y) {
        const db = (height / 2 - y) / (height / 2) * Theme.eqRangeDb
        return Math.round(Math.min(Theme.eqRangeDb, Math.max(-Theme.eqRangeDb, db)) * 10) / 10
    }
    function call(index, value) {
        return {id: "insert.setParam", params: {insert: insert.id, param: params[index].id, value: value}}
    }

    onCurveChanged: plot.requestPaint()
    onWidthChanged: plot.requestPaint()
    onHeightChanged: plot.requestPaint()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusClip
        color: Theme.graphite
        border.color: Theme.rule
    }

    Canvas {
        id: plot
        anchors.fill: parent
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.lineWidth = Theme.hairline
            for (const frequency of graph.frequencies) {
                const x = Math.round(graph.xOf(frequency)) + 0.5
                context.strokeStyle = Theme.rule
                context.beginPath()
                context.moveTo(x, 0)
                context.lineTo(x, height)
                context.stroke()
            }
            for (const level of graph.levels) {
                const y = Math.round(graph.yOf(level)) + 0.5
                context.strokeStyle = level === 0 ? Theme.inkDim : Theme.rule
                context.beginPath()
                context.moveTo(0, y)
                context.lineTo(width, y)
                context.stroke()
            }
            const points = graph.curve
            if (points.length < 2) {
                return
            }
            context.beginPath()
            context.moveTo(0, graph.yOf(0))
            for (const point of points) {
                context.lineTo(point.x * width, graph.yOf(point.db))
            }
            context.lineTo(width, graph.yOf(0))
            context.closePath()
            context.fillStyle = Theme.eqFill
            context.fill()
            context.beginPath()
            for (let index = 0; index < points.length; ++index) {
                const x = points[index].x * width
                const y = graph.yOf(points[index].db)
                if (index === 0) {
                    context.moveTo(x, y)
                } else {
                    context.lineTo(x, y)
                }
            }
            context.lineWidth = Theme.eqCurveWidth
            context.strokeStyle = Theme.ink
            context.stroke()
        }
    }

    Repeater {
        model: graph.frequencies
        Text {
            required property real modelData
            x: graph.xOf(modelData) + Theme.space1
            y: graph.height - height - Theme.space1
            text: modelData >= 1000 ? modelData / 1000 + "k" : modelData
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
            font.features: {"tnum": 1}
        }
    }
    Repeater {
        model: graph.levels.filter(level => level !== 0)
        Text {
            required property real modelData
            x: Theme.space1
            y: graph.yOf(modelData) - height / 2
            text: (modelData > 0 ? "+" : "") + modelData
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
            font.features: {"tnum": 1}
        }
    }

    Repeater {
        model: Theme.eqBands
        Rectangle {
            id: point
            required property int index
            readonly property int first: index * Theme.eqBandParams
            readonly property bool cut: index === 0 || index === Theme.eqBands - 1
            readonly property bool on: graph.valueOf(first) >= 0.5
            readonly property real frequency: graph.valueOf(first + 1)
            readonly property real gain: cut ? 0 : graph.valueOf(first + 2)
            readonly property color tint: Theme.trackColors[index % Theme.trackColors.length]
            objectName: "insert.setParam:" + graph.insert.id + ":" + graph.params[first].id.replace("On", "")
            x: graph.xOf(frequency) - width / 2
            y: graph.yOf(gain) - height / 2
            z: drag.pressed ? 2 : 1
            width: Theme.eqPoint
            height: Theme.eqPoint
            radius: width / 2
            color: on ? tint : Theme.graphite
            border.width: Theme.hairline * 2
            border.color: drag.containsMouse || drag.pressed ? Theme.ink : tint
            opacity: on ? 1 : Theme.mutedOpacity * 2

            ToolTip.text: graph.params[first].title + "   " + (frequency >= 1000 ? (frequency / 1000).toFixed(2) + " kHz"
                          : frequency.toFixed(0) + " Hz") + (cut ? "" : "   " + gain.toFixed(1) + " dB")
            ToolTip.visible: drag.containsMouse || drag.pressed
            ToolTip.delay: drag.pressed ? 0 : Theme.tooltipDelay

            MouseArea {
                id: drag
                objectName: point.objectName + ":drag"
                anchors.fill: parent
                anchors.margins: -Theme.space1
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                onPositionChanged: mouse => {
                    if (!pressed) {
                        return
                    }
                    const at = mapToItem(graph, mouse.x, mouse.y)
                    const calls = [graph.call(point.first + 1, graph.frequencyAt(at.x))]
                    if (!point.cut) {
                        calls.push(graph.call(point.first + 2, graph.dbAt(at.y)))
                    }
                    if (!point.on) {
                        calls.push(graph.call(point.first, 1))
                    }
                    Registry.updateGesture(Registry.actionInfo("insert.setParam").title, calls)
                }
                onReleased: Registry.endGesture()
                onCanceled: {
                    Registry.cancelGesture()
                    Registry.endGesture()
                }
                onDoubleClicked: Registry.runAction("insert.setParam", graph.call(point.first, point.on ? 0 : 1).params)
                onWheel: wheel => {
                    const q = graph.valueOf(point.first + 3)
                    const next = q * Math.pow(Theme.eqWheelQ, wheel.angleDelta.y / 120)
                    Registry.runAction("insert.setParam", graph.call(point.first + 3, Number(next.toPrecision(3))).params)
                }
            }
        }
    }
}
