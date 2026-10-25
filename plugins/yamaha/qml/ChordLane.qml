import QtQuick
import Emerick.App
import Emerick.Ui
import Emerick.Yamaha

BlockLane {
    id: chordLane
    property var trackData
    actionPrefix: "yamaha.chord"
    contextMenu: "Chord context"
    blocks: LaneBlocks.chordBlocks(trackData)
    picker: Component {
        Column {
            id: chordPicker
            readonly property var parts: LaneBlocks.chordParts(chordLane.editLabel)
            readonly property string fieldId: "yamaha.chord.set:" + chordLane.trackId + ":"
            spacing: Theme.space2

            function pick(root, quality, bass) {
                chordLane.set({chord: quality === "N.C." ? quality : (root || "C") + quality + (bass ? "/" + bass : "")})
            }

            Row {
                spacing: Theme.space1
                SearchCombo {
                    id: rootPicker
                    objectName: chordPicker.fieldId + "root"
                    width: Theme.chordPickerWidth
                    model: LaneBlocks.chordRoots()
                    currentId: chordPicker.parts.root
                    missingText: "Root"
                    acceptsText: true
                    toolTip: "Pick the root, or type a chord such as Bbm7/D"
                    deletesWhenUntouched: true
                    onDeleteRequested: chordLane.deleteEdited()
                    onPicked: entry => {
                        const quality = chordPicker.parts.quality === "N.C." ? "" : chordPicker.parts.quality
                        chordPicker.pick(entry.id, quality, chordPicker.parts.bass)
                        qualityPicker.open()
                    }
                    onTyped: text => {
                        chordLane.set({chord: text})
                        chordLane.closePicker()
                    }
                    Component.onCompleted: Qt.callLater(open)
                }
                SearchCombo {
                    id: qualityPicker
                    objectName: chordPicker.fieldId + "quality"
                    width: Theme.chordPickerWidth
                    model: LaneBlocks.chordQualities()
                    currentId: chordPicker.parts.quality
                    toolTip: "Pick the chord type"
                    deletesWhenUntouched: true
                    onDeleteRequested: chordLane.deleteEdited()
                    onPicked: entry => {
                        chordPicker.pick(chordPicker.parts.root, entry.id, chordPicker.parts.bass)
                        if (entry.id === "N.C.") {
                            chordLane.closePicker()
                        } else {
                            bassPicker.open()
                        }
                    }
                }
                SearchCombo {
                    id: bassPicker
                    objectName: chordPicker.fieldId + "bass"
                    width: Theme.chordPickerWidth
                    model: LaneBlocks.chordBasses()
                    currentId: chordPicker.parts.bass
                    toolTip: "Pick a bass note under the chord"
                    deletesWhenUntouched: true
                    onDeleteRequested: chordLane.deleteEdited()
                    onPicked: entry => {
                        chordPicker.pick(chordPicker.parts.root, chordPicker.parts.quality, entry.id)
                        chordLane.closePicker()
                    }
                }
            }
            Text {
                text: "Play a chord on the keyboard to set it"
                color: Theme.inkDim
                font.pixelSize: Theme.textSmall
            }
        }
    }
}
