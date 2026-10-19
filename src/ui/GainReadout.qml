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
    actionsFor: number => [{id: "track.setGain", params: {track: trackId, db: number}}]
}
