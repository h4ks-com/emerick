import QtQuick
import Emerick.App
import Emerick.Ui
import Emerick.Yamaha

BlockLane {
    id: styleLane
    property var trackData
    actionPrefix: "yamaha.section"
    contextMenu: "Section context"
    blocks: LaneBlocks.sectionBlocks(trackData)
    picker: Component {
        SearchCombo {
            objectName: "yamaha.section.set:" + styleLane.trackId
            width: Theme.labelWidth
            model: LaneBlocks.sectionChoices()
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
