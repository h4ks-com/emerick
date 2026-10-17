import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

Rectangle {
    id: trackList
    objectName: "tracks"
    color: Theme.graphite

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: Tracks
        delegate: Rectangle {
            id: row
            required property string trackId
            required property string name
            required property int colorIndex
            required property bool mute
            required property bool solo
            required property bool armed
            required property var clips
            width: ListView.view.width
            height: Theme.rowHeight
            color: Theme.graphite

            Rectangle {
                id: header
                width: Theme.trackHeaderWidth
                height: parent.height
                color: Nav.selectedTrack === row.trackId ? Theme.lift : Theme.slate
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2
                    anchors.rightMargin: Theme.space2
                    spacing: Theme.space1
                    ChromeButton {
                        objectName: "track.setArm:" + row.trackId
                        actionId: "track.setArm"
                        params: ({track: row.trackId, value: !row.armed})
                        text: "R"
                        active: row.armed
                        activeColor: Theme.rec
                    }
                    ChromeButton {
                        objectName: "track.setMute:" + row.trackId
                        actionId: "track.setMute"
                        params: ({track: row.trackId, value: !row.mute})
                        text: "M"
                        active: row.mute
                    }
                    ChromeButton {
                        objectName: "track.setSolo:" + row.trackId
                        actionId: "track.setSolo"
                        params: ({track: row.trackId, value: !row.solo})
                        text: "S"
                        active: row.solo
                    }
                    ChromeButton {
                        objectName: "track.select:" + row.trackId
                        actionId: "track.select"
                        params: ({track: row.trackId})
                        text: row.name
                        Layout.fillWidth: true
                    }
                }
            }

            Item {
                anchors.left: header.right
                anchors.right: parent.right
                height: parent.height
                clip: true
                Repeater {
                    model: row.clips
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "clip:" + modelData.id
                        x: modelData.start * Theme.pixelsPerTick
                        y: Theme.space1
                        width: Math.max(2 * Theme.hairline, modelData.length * Theme.pixelsPerTick)
                        height: row.height - 2 * Theme.space1
                        radius: Theme.radiusClip
                        color: Theme.trackColors[row.colorIndex % Project.trackColorCount]
                        opacity: row.mute ? Theme.mutedOpacity : 1
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: Theme.rule
            }
        }
    }

    Rectangle {
        objectName: "playhead"
        x: Theme.trackHeaderWidth + Project.positionTicks * Theme.pixelsPerTick
        width: Theme.hairline
        height: parent.height
        color: Theme.tape
        visible: list.count > 0
    }

    Column {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: Theme.space3
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Add a track to start recording"
            color: Theme.inkDim
            font.pixelSize: Theme.textTitle
        }
        ChromeButton {
            anchors.horizontalCenter: parent.horizontalCenter
            objectName: "track.addAudio"
        }
    }
}
