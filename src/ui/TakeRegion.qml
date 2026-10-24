import QtQuick
import Emerick.App

// A take while it records, from where recording started to how far it reached, until it lands as a clip.
Item {
    id: region

    required property var take
    required property string title
    property real pixelsPerTick
    property real viewFrom
    property real viewTo
    readonly property bool audio: take.type === "audio"

    x: take.start * pixelsPerTick
    width: Math.max(2 * Theme.hairline, take.length * pixelsPerTick)

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: Theme.clipInset
        anchors.bottomMargin: Theme.clipInset + Theme.hairline
        radius: Theme.radiusClip
        color: Theme.takePreview
        border.width: Theme.hairline
        border.color: Theme.rec
        clip: true

        Text {
            width: parent.width
            height: Theme.clipLabelHeight
            leftPadding: Theme.space1 + Math.max(0, (region.viewFrom - region.take.start) * region.pixelsPerTick)
            rightPadding: Theme.space1
            verticalAlignment: Text.AlignVCenter
            text: region.title
            color: Theme.ink
            font.pixelSize: Theme.textSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Item {
            y: Theme.clipLabelHeight
            width: parent.width
            height: parent.height - Theme.clipLabelHeight
            clip: true

            Waveform {
                objectName: "takeWaveform"
                visible: region.audio
                width: parent.width
                height: parent.height
                peaks: Peaks
                file: region.take.file
                frames: region.take.frames
                color: Theme.clipInk
                visibleLeft: (region.viewFrom - region.take.start) * region.pixelsPerTick
                visibleRight: (region.viewTo - region.take.start) * region.pixelsPerTick
            }

            ClipNotes {
                objectName: "takeNotes"
                visible: !region.audio
                width: parent.width
                height: parent.height
                notes: region.take.notes
                length: region.take.length
                color: Theme.clipInk
                textureSize: Qt.size(Math.max(1, Math.min(Math.ceil(width), Theme.maxTextureWidth)),
                                     Math.max(1, Math.ceil(height)))
            }
        }
    }
}
