import QtQuick
import Emerick.App

// A small pan knob after Logic's: drag sideways, or up and down outside a DragGroup, to pan, and double-click or
// Alt-click to center.
PanNumber {
    id: knob
    objectName: "track.setPan:" + trackId + ":knob"
    framed: false
    showsText: false
    dragsVertically: true
    resetValue: 0
    step: Theme.panKnobStep
    toolTip: Registry.actionInfo(actionId).title + "   " + textFromValue(shown)
    implicitWidth: Theme.panKnob
    implicitHeight: Theme.panKnob

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height)
        height: width
        radius: width / 2
        color: knob.held ? Theme.rule : Theme.lift
        border.width: Theme.hairline
        border.color: knob.gathered ? Theme.ink : Theme.rule

        Item {
            anchors.fill: parent
            rotation: knob.shown * Theme.panKnobSweep
            Rectangle {
                x: (parent.width - width) / 2
                y: Theme.hairline * 2
                width: Theme.hairline * 2
                height: parent.height / 2 - Theme.hairline * 2
                radius: width / 2
                color: knob.shown === 0 ? Theme.inkDim : Theme.ink
            }
        }
    }
}
