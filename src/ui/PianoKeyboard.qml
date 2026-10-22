import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The piano roll's keys, drawn as a real keyboard and labeled on each C. Pressing a key plays it on the track's MIDI
// output while held.
Rectangle {
    id: keyboard

    required property PianoRoll roll
    property int playing: -1

    readonly property real blackWidth: width * Theme.blackKeyLength
    readonly property var pitches: Array.from({length: 128}, (_, pitch) => pitch)
    // Three white keys share the five semitones from C to E and four share the seven from F to B, as on a piano.
    readonly property var whiteBands: ({0: [0, 5 / 3], 2: [5 / 3, 10 / 3], 4: [10 / 3, 5], 5: [5, 6.75], 7: [6.75, 8.5],
                                        9: [8.5, 10.25], 11: [10.25, 12]})

    function octaveBottom(pitch) {
        return roll.yOf(Math.floor(pitch / 12) * 12) + roll.keyHeight
    }
    function whiteTop(pitch) {
        return octaveBottom(pitch) - whiteBands[pitch % 12][1] * roll.keyHeight
    }
    function whiteHeight(pitch) {
        const band = whiteBands[pitch % 12]
        return (band[1] - band[0]) * roll.keyHeight
    }
    function keyAt(x, y) {
        const pitch = roll.pitchAt(y)
        if (roll.isBlack(pitch) && x <= blackWidth) {
            return pitch
        }
        const octave = Math.floor(pitch / 12) * 12
        const rows = (octaveBottom(pitch) - y) / roll.keyHeight
        for (const semitone of [0, 2, 4, 5, 7, 9, 11]) {
            if (rows < whiteBands[semitone][1]) {
                return Math.min(127, octave + semitone)
            }
        }
        return Math.min(127, octave + 11)
    }

    color: Theme.whiteKey
    clip: true

    component Key: Rectangle {
        required property int modelData
        readonly property int pitch: modelData
        objectName: "midi.audition:" + pitch
        visible: y + height > 0 && y < keyboard.height
        color: keyboard.playing === pitch ? Theme.tape : keyboard.roll.isBlack(pitch) ? Theme.blackKey : Theme.whiteKey
    }

    Repeater {
        model: keyboard.pitches.filter(pitch => !keyboard.roll.isBlack(pitch))
        delegate: Key {
            id: white
            y: keyboard.whiteTop(pitch)
            width: keyboard.width
            height: keyboard.whiteHeight(pitch)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: Theme.inkDim
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space1
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.hairline
                visible: white.pitch % 12 === 0 && keyboard.roll.keyHeight >= Theme.textSmall / 2
                text: keyboard.roll.noteName(white.pitch)
                color: Theme.graphite
                font.pixelSize: Theme.textSmall
            }
        }
    }

    Repeater {
        model: keyboard.pitches.filter(pitch => keyboard.roll.isBlack(pitch))
        delegate: Key {
            y: keyboard.roll.yOf(pitch)
            width: keyboard.blackWidth
            height: keyboard.roll.keyHeight
            radius: Theme.radiusClip
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
        ToolTip.text: "Play " + keyboard.roll.noteName(keyboard.keyAt(mouseX, mouseY))

        function send(pitch, on) {
            Registry.runAction("midi.audition", {track: keyboard.roll.clip.trackId, pitch: pitch, on: on})
        }

        onPressed: mouse => {
            keyboard.playing = keyboard.keyAt(mouse.x, mouse.y)
            send(keyboard.playing, true)
        }
        onPositionChanged: mouse => {
            const pitch = keyboard.keyAt(mouse.x, mouse.y)
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
