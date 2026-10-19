import QtQuick
import Emerick.App

// A panel docked at the bottom of the window. Dragging its top edge resizes it.
Rectangle {
    id: panel
    color: Theme.graphite

    signal resizeBy(real delta)

    Rectangle {
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }

    MouseArea {
        objectName: panel.objectName + ".resize"
        y: -Theme.splitterGrip / 2
        width: parent.width
        height: Theme.splitterGrip
        z: 1
        cursorShape: Qt.SizeVerCursor
        property real pressY
        onPressed: mouse => pressY = mouse.y
        onPositionChanged: mouse => panel.resizeBy(pressY - mouse.y)
    }
}
