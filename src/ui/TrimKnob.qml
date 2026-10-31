import QtQuick
import Emerick.App

// A track's input trim as a small knob whose ring grows from the top toward a cut or a boost: drag sideways, or up and
// down outside a DragGroup, to trim, and double-click or Alt-click to reset it to 0 dB.
DragNumber {
    id: knob
    required property string trackId
    required property real trimDb
    objectName: "track.setTrim:" + trackId + ":knob"
    actionId: "track.setTrim"
    value: trimDb
    from: -Theme.trimMaxDb
    to: Theme.trimMaxDb
    step: Theme.gainStepDb
    decimals: 1
    framed: false
    showsText: false
    dragsVertically: true
    resetValue: 0
    textFromValue: number => (number > 0 ? "+" : "") + number.toFixed(1) + " dB"
    toolTip: "Trim the track's input level   " + textFromValue(shown)
    actionsFor: number => [{id: "track.setTrim", params: {track: trackId, db: number, selection: true}}]
    implicitWidth: Theme.panKnob
    implicitHeight: Theme.panKnob

    Canvas {
        anchors.fill: parent
        readonly property real at: knob.shown / Theme.trimMaxDb
        readonly property bool active: knob.held || knob.gathered
        onAtChanged: requestPaint()
        onActiveChanged: requestPaint()
        onPaint: {
            const context = getContext("2d")
            const ring = Theme.hairline * 2
            const radius = Math.min(width, height) / 2 - ring / 2
            const top = -Math.PI / 2
            const sweep = Theme.panKnobSweep * Math.PI / 180
            context.reset()
            context.lineWidth = ring
            context.strokeStyle = Theme.graphite
            context.beginPath()
            context.arc(width / 2, height / 2, radius, top - sweep, top + sweep)
            context.stroke()
            if (at !== 0) {
                context.strokeStyle = Theme.ink
                context.beginPath()
                context.arc(width / 2, height / 2, radius, top, top + at * sweep, at < 0)
                context.stroke()
            }
            context.fillStyle = active ? Theme.rule : Theme.lift
            context.beginPath()
            context.arc(width / 2, height / 2, radius - ring, 0, Math.PI * 2)
            context.fill()
        }
    }
}
