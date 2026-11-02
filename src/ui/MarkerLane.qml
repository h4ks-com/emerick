import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The marker track under the bar ruler, with one named flag per marker.
Rectangle {
    id: lane
    objectName: "markers"

    property real scrollTicks
    property real pixelsPerTick
    property real menuAt: 0
    property string menuMarker

    color: Theme.slate
    clip: true

    function ticksAt(x, modifiers) {
        return Project.placed(Math.round(scrollTicks + x / pixelsPerTick), modifiers)
    }

    ActionMenu {
        id: laneMenu
        objectName: "menu.markerLaneContext"
        menuName: "Marker lane context"
        params: ({at: lane.menuAt})
    }
    ActionMenu {
        id: markerMenu
        objectName: "menu.markerContext"
        menuName: "Marker context"
        params: ({marker: lane.menuMarker})
    }

    MouseArea {
        objectName: "marker.add"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            if (Theme.isContextClick(mouse)) {
                lane.menuAt = lane.ticksAt(mouse.x, mouse.modifiers)
                laneMenu.popup()
            }
        }
        onDoubleClicked: mouse => Registry.runAction("marker.add", {at: lane.ticksAt(mouse.x, mouse.modifiers)})
    }

    // We count the markers rather than list them, so an edit keeps the flags and a drag survives it.
    Repeater {
        model: Project.markers.length
        delegate: Item {
            id: flag
            required property int index
            readonly property var marker: Project.markers[index] ?? {id: "", name: "", at: 0, color: null}
            readonly property bool selected: Nav.selectedMarker === marker.id
            readonly property real fullWidth: label.implicitWidth + 2 * Theme.space1
            readonly property real roomToNext: index + 1 < Project.markers.length
                                               ? (Project.markers[index + 1].at - marker.at) * lane.pixelsPerTick
                                               : fullWidth
            readonly property bool raised: grab.containsMouse || selected || rename.visible
            objectName: "marker:" + marker.id
            x: (marker.at - lane.scrollTicks) * lane.pixelsPerTick
            z: raised ? 1 : 0
            width: Math.max(raised ? fullWidth : Math.max(2 * Theme.space1, Math.min(fullWidth, roomToNext)),
                            rename.visible ? Theme.labelWidth : 0)
            height: lane.height

            Rectangle {
                width: Theme.hairline
                height: parent.height
                color: Theme.ink
            }
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: Theme.hairline
                anchors.bottomMargin: Theme.hairline + Theme.hairline
                radius: Theme.radiusClip / 2
                color: flag.marker.color === null ? Theme.lift : Theme.trackColors[flag.marker.color % Project.trackColorCount]
                border.width: flag.selected ? Theme.hairline : 0
                border.color: Theme.tape
            }
            Text {
                id: label
                objectName: "marker.name:" + flag.marker.id
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space1
                width: flag.width - 2 * Theme.space1
                visible: !rename.visible
                text: flag.marker.name
                elide: Text.ElideRight
                color: Theme.ink
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
            }

            MouseArea {
                id: grab
                objectName: "marker.move:" + flag.marker.id
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property string markerId
                property real pressX
                property real pressAt
                property bool dragging: false
                ToolTip.text: Registry.actionInfo("marker.move").title + ": " + flag.marker.name
                ToolTip.visible: containsMouse && !pressed
                ToolTip.delay: Theme.tooltipDelay

                onPressed: mouse => {
                    markerId = flag.marker.id
                    Registry.runAction("marker.select", {marker: markerId})
                    if (Theme.isContextClick(mouse)) {
                        lane.menuMarker = markerId
                        markerMenu.popup()
                        return
                    }
                    pressX = grab.mapToItem(null, mouse.x, 0).x
                    pressAt = flag.marker.at
                    dragging = false
                }
                onPositionChanged: mouse => {
                    if (!pressed) {
                        return
                    }
                    const dx = grab.mapToItem(null, mouse.x, 0).x - pressX
                    dragging = dragging || Math.abs(dx) >= Theme.dragThreshold
                    if (dragging) {
                        Registry.updateGesture(Registry.actionInfo("marker.move").title, [{
                            id: "marker.move",
                            params: {marker: markerId, at: Project.placed(Math.round(pressAt + dx / lane.pixelsPerTick), mouse.modifiers)}
                        }])
                    }
                }
                onReleased: Registry.endGesture()
                onCanceled: {
                    Registry.cancelGesture()
                    Registry.endGesture()
                }
                onDoubleClicked: Registry.runAction("marker.beginRename", {marker: flag.marker.id})
            }

            TextField {
                id: rename
                objectName: "marker.rename:" + flag.marker.id
                anchors.fill: parent
                visible: Nav.renamingMarker !== "" && Nav.renamingMarker === flag.marker.id
                ToolTip.text: Registry.actionInfo("marker.rename").title
                ToolTip.visible: hovered && !activeFocus
                ToolTip.delay: Theme.tooltipDelay
                padding: 0
                leftPadding: Theme.space1
                color: Theme.ink
                font.pixelSize: Theme.textSmall
                background: Rectangle {
                    color: Theme.graphite
                    radius: Theme.radiusClip / 2
                    border.color: Theme.rule
                }
                // A hidden field keeps the focus it had, which would turn the window's shortcuts off.
                onVisibleChanged: {
                    if (visible) {
                        text = flag.marker.name
                        forceActiveFocus()
                        selectAll()
                    } else {
                        focus = false
                    }
                }
                onAccepted: {
                    if (text.length > 0 && text !== flag.marker.name) {
                        Registry.runAction("marker.rename", {marker: flag.marker.id, name: text})
                    }
                    Nav.renamingMarker = ""
                }
                onActiveFocusChanged: if (!activeFocus && visible) Nav.renamingMarker = ""
                Keys.onEscapePressed: Nav.renamingMarker = ""
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }
}
