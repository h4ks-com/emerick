import QtQuick
import Emerick.App

// A take while it records, from where recording started to how far it reached, until it lands as a clip.
Item {
    id: region

    required property var take
    required property var frozen
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

            // Each frozen segment draws once into its own texture, and only the live chunk repaints as notes arrive.
            // We count the segments rather than list them, so a new segment keeps the items of the others.
            Repeater {
                model: region.frozen.length
                delegate: ClipNotes {
                    required property int index
                    readonly property var segment: region.frozen[index] ?? {start: 0, length: 0, notes: []}
                    objectName: "takeFrozenNotes:" + index
                    x: segment.start * region.pixelsPerTick
                    width: segment.length * region.pixelsPerTick
                    height: parent.height
                    notes: segment.notes
                    length: segment.length
                    lowKey: region.take.lowKey
                    highKey: region.take.highKey
                    color: Theme.clipInk
                    textureSize: Theme.notesTexture(width, height)
                }
            }
            ClipNotes {
                objectName: "takeNotes"
                visible: !region.audio
                x: region.take.liveFrom * region.pixelsPerTick
                width: parent.width - x
                height: parent.height
                notes: region.take.notes
                length: region.take.length - region.take.liveFrom
                lowKey: region.take.lowKey
                highKey: region.take.highKey
                color: Theme.clipInk
                textureSize: Theme.notesTexture(width, height)
            }
        }
    }
}
