import QtQuick
import Emerick.App

// A track's gain in dB as a number field beside its fader.
DragNumber {
    required property string trackId
    required property real gainDb
    objectName: "track.setGain:" + trackId + ":db"
    actionId: "track.setGain"
    value: gainDb
    from: Theme.gainMinDb
    to: Theme.gainMaxDb
    step: Theme.gainStepDb
    decimals: 1
    unit: "dB"
    framed: false
    textColor: Theme.inkDim
    font.pixelSize: Theme.textSmall
    implicitWidth: Theme.gainReadoutWidth
    implicitHeight: Theme.headerButton
    onCommitted: number => Registry.runAction("track.setGain", {track: trackId, db: number})
}
