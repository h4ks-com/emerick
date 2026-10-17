import QtQuick
import Emerick.Yamaha

BlockLane {
    property var trackData
    actionPrefix: "yamaha.section"
    nameParam: "name"
    blocks: LaneBlocks.sectionBlocks(trackData)
}
