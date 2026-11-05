import QtQuick
import Emerick.Ui

// A Chords or Style lane as the viewer shows it: each chord or section as a named block up to the next one.
Item {
    id: row
    property string trackId
    // The blocks the viewer lays out from the track's data.
    property var trackData: []
    property real pixelsPerTick: 0
    property real viewFrom: 0
    property real viewTo: 0

    Repeater {
        model: row.trackData.filter(block => block.end > row.viewFrom && block.at < row.viewTo)
        delegate: Rectangle {
            required property var modelData
            objectName: "lane.block:" + row.trackId + ":" + modelData.at
            x: modelData.at * row.pixelsPerTick + Theme.hairline
            y: Theme.space1
            width: Math.max(0, (modelData.end - modelData.at) * row.pixelsPerTick - 2 * Theme.hairline)
            height: row.height - 2 * Theme.space1
            radius: Theme.radiusClip
            color: modelData.gap ? "transparent" : Theme.trackColors[modelData.colorIndex % Theme.trackColors.length]
            border.width: modelData.gap ? Theme.hairline : 0
            border.color: Theme.rule

            Text {
                objectName: "lane.blockLabel:" + row.trackId + ":" + parent.modelData.at
                anchors.fill: parent
                anchors.leftMargin: Theme.space1
                anchors.rightMargin: Theme.space1
                verticalAlignment: Text.AlignVCenter
                text: parent.modelData.label
                color: parent.modelData.gap ? Theme.inkDim : Theme.ink
                font.pixelSize: Theme.textBody
                elide: Text.ElideRight
            }
        }
    }
}
