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
            onPicked: entry => {
                styleLane.set({name: entry.id})
                styleLane.closePicker()
            }
            Component.onCompleted: Qt.callLater(open)
        }
    }
}
