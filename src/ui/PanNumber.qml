import QtQuick
import Emerick.App

// A track's pan from -1 (hard left) to 1 (hard right), shown and typed as "L 23", "C" or "R 40".
DragNumber {
    required property string trackId
    required property real pan
    objectName: "track.setPan:" + trackId
    actionId: "track.setPan"
    value: pan
    from: -1
    to: 1
    step: Theme.panStep
    decimals: 2
    textFromValue: number => {
        const percent = Math.round(Math.abs(number) * 100)
        return percent === 0 ? "C" : (number < 0 ? "L " : "R ") + percent
    }
    valueFromText: text => {
        const typed = text.trim().toUpperCase()
        const side = /^([LR])\s*(\d+(\.\d+)?)$/.exec(typed)
        if (typed === "C") {
            return 0
        }
        return side ? (side[1] === "L" ? -1 : 1) * Number(side[2]) / 100 : Number(typed) / 100
    }
    actionsFor: number => [{id: "track.setPan", params: {track: trackId, value: number, selection: true}}]
}
