import QtQuick
import Emerick.App

// The piano roll's key rows, its grid, beat and bar lines, and the shade over time outside the clip.
Item {
    id: grid

    required property PianoRoll roll

    Repeater {
        model: 128
        delegate: Rectangle {
            required property int index
            y: grid.roll.yOf(index)
            width: grid.width
            height: grid.roll.keyHeight
            visible: y + height > 0 && y < grid.height
            color: grid.roll.isBlack(index) ? Theme.blackKeyRow : Theme.clear
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: index % 12 === 0 ? Theme.barLine : Theme.beatLine
            }
        }
    }

    Repeater {
        id: lines
        readonly property real step: grid.roll.gridTicks * grid.roll.pixelsPerTick >= Theme.minGridSpacing ? grid.roll.gridTicks
                                     : grid.roll.showBeats ? Project.ticksPerBeat : grid.roll.barStep * Project.ticksPerBar
        readonly property real first: Math.floor(grid.roll.scrollTicks / step)
        model: Math.ceil(grid.width / (step * grid.roll.pixelsPerTick)) + 1
        delegate: Rectangle {
            required property int index
            readonly property real at: (lines.first + index) * lines.step
            x: (at - grid.roll.scrollTicks) * grid.roll.pixelsPerTick
            width: Theme.hairline
            height: grid.height
            color: at % Project.ticksPerBar === 0 ? Theme.rule : at % Project.ticksPerBeat === 0 ? Theme.barLine : Theme.beatLine
        }
    }

    Rectangle {
        width: Math.max(0, Math.min(grid.width, grid.roll.xOf(0)))
        height: grid.height
        color: Theme.outsideClip
    }
    Rectangle {
        x: Math.max(0, grid.roll.xOf(grid.roll.clipLength))
        width: Math.max(0, grid.width - x)
        height: grid.height
        color: Theme.outsideClip
    }
}
