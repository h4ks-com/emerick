import QtQuick
import Emerick.App
import Emerick.Ui
import Emerick.Yamaha

BlockLane {
    id: styleLane
    property var trackData
    readonly property var style: ArrangerStyles.revision >= 0 ? ArrangerStyles.info(trackId) : ({})
    // The sections the track's Arranger style lacks, by the names the blocks show.
    readonly property var missing: style.missing ?? []
    actionPrefix: "yamaha.section"
    contextMenu: "Section context"
    blocks: LaneBlocks.sectionBlocks(trackData)
    picker: Component {
        SearchCombo {
            objectName: "yamaha.section.set:" + styleLane.trackId
            width: Theme.labelWidth
            model: LaneBlocks.sectionChoices().map(entry => Object.assign({dim: styleLane.missing.includes(entry.id)}, entry))
            currentId: styleLane.editLabel
            missingText: "Section"
            toolTip: "Pick the section"
            deletesWhenUntouched: true
            onDeleteRequested: styleLane.deleteEdited()
            onPicked: entry => {
                styleLane.set({name: entry.id})
                styleLane.closePicker()
            }
            Component.onCompleted: Qt.callLater(open)
        }
    }

    Repeater {
        model: styleLane.blocks.filter(block => styleLane.missing.includes(block.label))
        delegate: WarningBadge {
            required property var modelData
            objectName: "yamaha.section.missing:" + styleLane.trackId + ":" + modelData.at
            x: Math.max(modelData.at * styleLane.pixelsPerTick, modelData.end * styleLane.pixelsPerTick - width - Theme.space1)
            y: (styleLane.height - height) / 2
            message: styleLane.style.name + " has no " + modelData.label + ".\nPick a section the style plays."
        }
    }

    Repeater {
        model: styleLane.blocks.filter(block => block.patternBars > 0)
        delegate: Repeater {
            id: passes
            required property var modelData
            readonly property real passTicks: modelData.patternBars * Project.ticksPerBar
            model: Math.max(0, Math.ceil((modelData.end - modelData.at) / passTicks) - 1)
            delegate: Rectangle {
                required property int index
                x: (passes.modelData.at + (index + 1) * passes.passTicks) * styleLane.pixelsPerTick
                y: Theme.space1
                width: Theme.hairline
                height: styleLane.height - 2 * Theme.space1
                color: Theme.clipInk
            }
        }
    }
}
