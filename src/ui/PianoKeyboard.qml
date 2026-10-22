import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The piano roll's keys, labeled on each C. Pressing a key plays it on the track's MIDI output while held.
Rectangle {
    id: keyboard

    required property PianoRoll roll
    property int playing: -1

    color: Theme.whiteKey
    clip: true

    Repeater {
        model: 128
        delegate: Rectangle {
            required property int index
            readonly property bool black: keyboard.roll.isBlack(index)
            objectName: "midi.audition:" + index
            y: keyboard.roll.yOf(index)
            z: black ? 1 : 0
            width: black ? keyboard.width * Theme.blackKeyLength : keyboard.width
            height: keyboard.roll.keyHeight
            visible: y + height > 0 && y < keyboard.height
            color: keyboard.playing === index ? Theme.tape : black ? Theme.blackKey : Theme.whiteKey
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                visible: !parent.black
                color: Theme.inkDim
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space1
                anchors.verticalCenter: parent.verticalCenter
                visible: index % 12 === 0 && keyboard.roll.keyHeight >= Theme.textSmall
                text: keyboard.roll.noteName(index)
                color: Theme.graphite
                font.pixelSize: Theme.textSmall
            }
        }
    }

    MouseArea {
        id: keys
        objectName: "midi.audition"
        anchors.fill: parent
        hoverEnabled: true
        enabled: keyboard.roll.hasClip
        ToolTip.visible: containsMouse && !pressed
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.text: "Play " + keyboard.roll.noteName(keyboard.roll.pitchAt(mouseY))

        function send(pitch, on) {
            Registry.runAction("midi.audition", {track: keyboard.roll.clip.trackId, pitch: pitch, on: on})
        }

        onPressed: mouse => {
            keyboard.playing = keyboard.roll.pitchAt(mouse.y)
            send(keyboard.playing, true)
        }
        onPositionChanged: mouse => {
            const pitch = keyboard.roll.pitchAt(mouse.y)
            if (pressed && pitch !== keyboard.playing) {
                send(keyboard.playing, false)
                keyboard.playing = pitch
                send(pitch, true)
            }
        }
        onReleased: {
            send(keyboard.playing, false)
            keyboard.playing = -1
        }
        onCanceled: {
            send(keyboard.playing, false)
            keyboard.playing = -1
        }
    }
}
