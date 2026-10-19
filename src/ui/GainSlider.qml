import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A track's gain fader in dB, horizontal in a track header and vertical in a mixer strip. Releasing it runs
// track.setGain once, so a drag is one undo step.
Slider {
    id: gain
    required property string trackId
    required property real gainDb
    // The value to show, such as a linked readout's dragged value.
    property real shownDb: gainDb

    objectName: "track.setGain:" + trackId
    from: Theme.gainMinDb
    to: Theme.gainMaxDb
    value: shownDb
    padding: 0
    ToolTip.visible: pressed || hovered
    ToolTip.text: "Set track gain   " + value.toFixed(1) + " dB"
    onPressedChanged: {
        if (!pressed && value !== gainDb) {
            Registry.runAction("track.setGain", {track: trackId, db: value})
        }
    }
    background: Rectangle {
        x: gain.leftPadding + (gain.vertical ? (gain.availableWidth - width) / 2 : 0)
        y: gain.topPadding + (gain.vertical ? 0 : (gain.availableHeight - height) / 2)
        width: gain.vertical ? Theme.faderGroove : gain.availableWidth
        height: gain.vertical ? gain.availableHeight : Theme.faderGroove
        radius: Math.min(width, height) / 2
        color: Theme.graphite
        Rectangle {
            y: gain.vertical ? gain.visualPosition * parent.height : 0
            width: gain.vertical ? parent.width : gain.visualPosition * parent.width
            height: gain.vertical ? (1 - gain.visualPosition) * parent.height : parent.height
            radius: parent.radius
            color: Theme.inkDim
        }
    }
    handle: Rectangle {
        x: gain.leftPadding + (gain.vertical ? (gain.availableWidth - width) / 2
                                             : gain.visualPosition * (gain.availableWidth - width))
        y: gain.topPadding + (gain.vertical ? gain.visualPosition * (gain.availableHeight - height)
                                            : (gain.availableHeight - height) / 2)
        width: gain.vertical ? Theme.faderCapWidth : Theme.fadeHandle + Theme.space1
        height: gain.vertical ? Theme.faderCapHeight : width
        radius: gain.vertical ? Theme.hairline * 2 : width / 2
        border.width: gain.vertical ? Theme.hairline : 0
        border.color: Theme.graphite
        gradient: Gradient {
            GradientStop { position: 0; color: gain.pressed || gain.vertical ? Theme.ink : Theme.inkDim }
            GradientStop { position: 1; color: gain.pressed ? Theme.ink : Theme.inkDim }
        }
        Rectangle {
            visible: gain.vertical
            anchors.centerIn: parent
            width: parent.width - Theme.space2
            height: Theme.hairline
            color: Theme.graphite
        }
    }
}
