import QtQuick
import Emerick.App

// An insert param as a knob after Logic's, with its name above and its value below: drag the knob up or down to turn
// it, double-click or Alt-click it for the default, and click the value to type one.
Column {
    id: knob
    required property string insertId
    required property var param
    required property real paramValue
    spacing: Theme.space1
    width: Theme.effectKnobWidth

    Text {
        width: parent.width
        text: knob.param.title
        color: Theme.inkDim
        font.pixelSize: Theme.textSmall
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    ParamField {
        id: dial
        objectName: "insert.setParam:" + knob.insertId + ":" + knob.param.id + ":knob"
        insertId: knob.insertId
        param: knob.param
        paramValue: knob.paramValue
        framed: false
        showsText: false
        dragsVertically: true
        resetValue: position(knob.param.defaultValue)
        toolTip: knob.param.title + "   " + format(valueAt(shown))
        width: Theme.effectKnob
        height: Theme.effectKnob
        anchors.horizontalCenter: parent.horizontalCenter

        Canvas {
            id: face
            anchors.fill: parent
            readonly property real at: dial.shown
            readonly property bool active: dial.held || dial.gathered
            onAtChanged: requestPaint()
            onActiveChanged: requestPaint()
            onPaint: {
                const context = getContext("2d")
                const radius = width / 2 - Theme.knobRing
                const start = Math.PI * 0.75
                const end = start + Math.PI * 1.5 * at
                context.reset()
                context.lineCap = "round"
                context.lineWidth = Theme.knobRing
                context.strokeStyle = Theme.graphite
                context.beginPath()
                context.arc(width / 2, height / 2, radius, start, start + Math.PI * 1.5)
                context.stroke()
                context.strokeStyle = Theme.ink
                context.beginPath()
                context.arc(width / 2, height / 2, radius, start, end)
                context.stroke()
                context.fillStyle = active ? Theme.rule : Theme.lift
                context.beginPath()
                context.arc(width / 2, height / 2, radius - Theme.knobRing, 0, Math.PI * 2)
                context.fill()
                context.strokeStyle = Theme.ink
                context.lineWidth = Theme.hairline * 2
                context.beginPath()
                context.moveTo(width / 2 + Math.cos(end) * (radius - Theme.knobRing * 4),
                               height / 2 + Math.sin(end) * (radius - Theme.knobRing * 4))
                context.lineTo(width / 2 + Math.cos(end) * (radius - Theme.knobRing),
                               height / 2 + Math.sin(end) * (radius - Theme.knobRing))
                context.stroke()
            }
        }
    }

    ParamField {
        objectName: "insert.setParam:" + knob.insertId + ":" + knob.param.id
        insertId: knob.insertId
        param: knob.param
        paramValue: knob.paramValue
        framed: false
        width: parent.width
        height: Theme.headerButton
        font.pixelSize: Theme.textSmall
    }
}
