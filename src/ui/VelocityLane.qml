import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A bar per note as tall as its velocity. Dragging a bar up or down sets the velocity of its note, or of every selected
// note when its note is selected.
Item {
    id: lane

    required property PianoRoll roll

    Item {
        id: bars
        anchors.fill: parent

        Repeater {
            model: lane.roll.notes.length
            delegate: Rectangle {
                required property int index
                readonly property var note: lane.roll.notes[index] ?? {id: "", velocity: 1, start: 0}
                objectName: "midi.setVelocity:" + note.id
                x: lane.roll.xOf(note.start)
                y: bars.height - height
                width: Theme.velocityBar
                height: Math.max(Theme.hairline, bars.height * note.velocity / 127)
                visible: x + width > 0 && x < bars.width
                radius: Theme.hairline
                color: lane.roll.selected(note.id) ? Theme.selectedNote(note.velocity) : Theme.velocityColor(note.velocity)
            }
        }

        MouseArea {
            objectName: "midi.setVelocity"
            anchors.fill: parent
            enabled: lane.roll.hasClip && !Registry.readOnly
            property var pressNote: null
            property var targets: []
            property real pressY
            readonly property var heldNote: pressNote ? lane.roll.notes.find(note => note.id === pressNote.id) ?? null : null
            ToolTip.visible: heldNote !== null
            ToolTip.text: heldNote ? lane.roll.noteName(heldNote.pitch) + "   velocity " + heldNote.velocity : ""

            function noteNear(x) {
                const ticks = lane.roll.ticksAt(x)
                const slack = Theme.velocityBar / lane.roll.pixelsPerTick
                let best = null
                for (const note of lane.roll.notes) {
                    const distance = Math.abs(ticks - note.start)
                    if (distance <= slack + Theme.space1 / lane.roll.pixelsPerTick
                            && (best === null || distance < Math.abs(ticks - best.start))) {
                        best = note
                    }
                }
                return best
            }

            onPressed: mouse => {
                lane.roll.forceActiveFocus()
                pressNote = noteNear(mouse.x)
                if (!pressNote) {
                    return
                }
                pressY = mouse.y
                if (!lane.roll.selected(pressNote.id)) {
                    Registry.runAction("midi.selectNotes", {notes: [pressNote.id]})
                }
                targets = Array.from(Nav.selectedNotes)
            }
            onPositionChanged: mouse => {
                if (!pressNote) {
                    return
                }
                const target = Math.max(1, Math.min(127, Math.round(pressNote.velocity + (pressY - mouse.y) / bars.height * 127)))
                Registry.updateGesture(Registry.actionInfo("midi.setVelocity").title,
                                       [{id: "midi.setVelocity", params: {notes: targets, by: target - pressNote.velocity}}])
            }
            onReleased: {
                if (pressNote) {
                    Registry.endGesture()
                }
                pressNote = null
            }
            onCanceled: {
                if (pressNote) {
                    Registry.cancelGesture()
                    Registry.endGesture()
                }
                pressNote = null
            }
        }
    }
}
