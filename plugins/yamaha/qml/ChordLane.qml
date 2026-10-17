import QtQuick
import Emerick.Yamaha

BlockLane {
    property var trackData
    actionPrefix: "yamaha.chord"
    nameParam: "chord"
    blocks: LaneBlocks.chordBlocks(trackData)
}
