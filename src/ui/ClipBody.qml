import QtQuick
import QtQuick.Shapes
import Emerick.App

Item {
    id: content

    required property ClipItem clipItem

    component FadeHandle: Rectangle {
        id: handle
        required property ClipItem clipItem
        required property bool fadeOut
        required property bool shown
        readonly property alias pressed: grab.pressed
        y: Theme.clipInset
        width: Theme.fadeHandle
        height: Theme.fadeHandle
        radius: Theme.hairline
        color: Theme.ink
        border.color: Theme.graphite
        border.width: Theme.hairline
        visible: clipItem.audio && (shown || pressed)

        MouseArea {
            id: grab
            objectName: "clip.setFade:" + handle.clipItem.clipId + (handle.fadeOut ? ":out" : ":in")
            anchors.fill: parent
            anchors.margins: -Theme.space1
            cursorShape: Qt.SizeHorCursor
            property real pressX
            property real pressFade
            onPressed: mouse => {
                pressX = handle.clipItem.sceneX(grab, mouse)
                pressFade = handle.fadeOut ? handle.clipItem.fadeOut : handle.clipItem.fadeIn
            }
            onCanceled: handle.clipItem.cancelDrag()
            onPositionChanged: mouse => {
                const item = handle.clipItem
                const ticks = (item.sceneX(grab, mouse) - pressX) / item.pixelsPerTick
                if (handle.fadeOut) {
                    const fade = item.end - item.place(item.end - pressFade + ticks, mouse.modifiers)
                    item.drag("clip.setFade", {fadeOut: Math.max(0, Math.min(fade, item.clip.length - item.fadeIn))})
                } else {
                    const fade = item.place(item.start + pressFade + ticks, mouse.modifiers) - item.start
                    item.drag("clip.setFade", {fadeIn: Math.max(0, Math.min(fade, item.clip.length - item.fadeOut))})
                }
            }
            onReleased: Registry.endGesture()
        }
    }

    Binding {
        target: content.clipItem
        property: "editing"
        value: move.pressed || startGrip.pressed || endGrip.pressed || fadeInHandle.pressed || fadeOutHandle.pressed
    }

    HoverHandler {
        id: hover
    }

    Rectangle {
        id: body
        anchors.fill: parent
        anchors.topMargin: Theme.clipInset
        anchors.bottomMargin: Theme.clipInset + Theme.hairline
        radius: Theme.radiusClip
        color: content.clipItem.trackColor
        clip: true

        Rectangle {
            width: parent.width
            height: Theme.clipLabelHeight
            radius: Theme.radiusClip
            color: Theme.clipLabel
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.radius
                color: Theme.clipLabel
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: Theme.fadeHandle + Theme.space1
                anchors.rightMargin: Theme.space1
                verticalAlignment: Text.AlignVCenter
                text: content.clipItem.trackName + "   Take " + content.clipItem.clip.take
                color: Theme.ink
                font.pixelSize: Theme.textSmall
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }

        Item {
            y: Theme.clipLabelHeight
            width: parent.width
            height: parent.height - Theme.clipLabelHeight
            clip: true

            Loader {
                active: content.clipItem.audio
                width: content.clipItem.clip.length * content.clipItem.pixelsPerTick
                height: parent.height
                sourceComponent: Waveform {
                    objectName: "waveform:" + content.clipItem.clipId
                    peaks: Peaks
                    file: content.clipItem.clip.file
                    offset: content.clipItem.clip.sourceOffset
                    frames: content.clipItem.clip.frames
                    color: Theme.clipInk
                    visibleLeft: (content.clipItem.viewFrom - content.clipItem.start) * content.clipItem.pixelsPerTick
                    visibleRight: (content.clipItem.viewTo - content.clipItem.start) * content.clipItem.pixelsPerTick
                }
            }

            Loader {
                active: !content.clipItem.audio
                width: content.clipItem.clip.length * content.clipItem.pixelsPerTick
                height: parent.height
                sourceComponent: ClipNotes {
                    notes: content.clipItem.clip.notes
                    length: content.clipItem.clip.length
                    color: Theme.clipInk
                    textureSize: Qt.size(Math.max(1, Math.min(Math.ceil(width), Theme.maxTextureWidth)),
                                         Math.max(1, Math.ceil(height)))
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: content.clipItem.fadeIn > 0 || content.clipItem.fadeOut > 0
            sourceComponent: Shape {
                readonly property real inX: content.clipItem.fadeIn * content.clipItem.pixelsPerTick
                readonly property real outX: width - content.clipItem.fadeOut * content.clipItem.pixelsPerTick
                ShapePath {
                    fillColor: Theme.fadeShade
                    strokeColor: Theme.fadeLine
                    strokeWidth: Theme.hairline
                    startX: 0
                    startY: body.height
                    PathLine { x: inX; y: 0 }
                    PathLine { x: 0; y: 0 }
                }
                ShapePath {
                    fillColor: Theme.fadeShade
                    strokeColor: Theme.fadeLine
                    strokeWidth: Theme.hairline
                    startX: body.width
                    startY: body.height
                    PathLine { x: outX; y: 0 }
                    PathLine { x: body.width; y: 0 }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: body
        radius: Theme.radiusClip
        color: Theme.clear
        border.width: Theme.selectionWidth
        border.color: Theme.tape
        visible: content.clipItem.selected
    }

    MouseArea {
        id: move
        objectName: "clip.move:" + content.clipItem.clipId
        anchors.fill: parent
        anchors.leftMargin: Theme.clipGrip
        anchors.rightMargin: Theme.clipGrip
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property point pressAt
        property real pressStart
        property real target
        property bool dragging: false
        property int rows: 0
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

        function endDrag() {
            dragging = false
            rows = 0
            content.clipItem.timeline.dragRows = 0
        }

        onPressed: mouse => {
            const item = content.clipItem
            if (Theme.isContextClick(mouse)) {
                if (!item.selected) {
                    Registry.runAction("selection.set", {clips: [item.clipId]})
                }
                item.timeline.openClipMenu()
                mouse.accepted = false
                return
            }
            pressAt = move.mapToItem(null, mouse.x, mouse.y)
            pressStart = item.start
            target = item.start
            dragging = false
            item.selectOnPress(mouse.modifiers)
        }
        onCanceled: {
            content.clipItem.cancelDrag()
            endDrag()
        }
        onPositionChanged: mouse => {
            const item = content.clipItem
            const at = move.mapToItem(null, mouse.x, mouse.y)
            const dx = at.x - pressAt.x
            const dy = at.y - pressAt.y
            dragging = dragging || Math.abs(dx) >= Theme.dragThreshold || Math.abs(dy) >= Theme.dragThreshold
            if (!dragging) {
                return
            }
            target = item.place(pressStart + dx / item.pixelsPerTick, mouse.modifiers)
            item.drag("clip.move", {start: target, selection: true})
            const wanted = Math.round(dy / Nav.trackHeight)
            rows = item.timeline.trackIdAt(item.rowIndex + wanted) !== "" ? wanted : rows
            item.timeline.dragFrom = item.rowIndex
            item.timeline.dragType = item.clip.type
            item.timeline.dragRows = rows
        }
        // We move the clip to another track only on release, because that rebuilds both rows and this item with them.
        onReleased: {
            const item = content.clipItem
            const row = item.rowIndex + rows
            const accepted = rows === 0 || item.timeline.rowAccepts(row, item.clip.type)
            const track = rows !== 0 ? item.timeline.trackIdAt(row) : ""
            endDrag()
            if (!accepted) {
                Registry.cancelGesture()
            } else if (track !== "") {
                item.drag("clip.move", {start: target, selection: true, track: track})
            }
            Registry.endGesture()
        }
    }

    MouseArea {
        id: startGrip
        objectName: "clip.resize:" + content.clipItem.clipId + ":left"
        width: Theme.clipGrip
        height: parent.height
        cursorShape: Qt.SizeHorCursor
        property real pressX
        property real pressStart
        property real pressEnd
        onPressed: mouse => {
            pressX = content.clipItem.sceneX(startGrip, mouse)
            pressStart = content.clipItem.start
            pressEnd = content.clipItem.end
        }
        onPositionChanged: mouse => {
            const item = content.clipItem
            const target = item.place(pressStart + (item.sceneX(startGrip, mouse) - pressX) / item.pixelsPerTick,
                                      mouse.modifiers)
            item.drag("clip.resize", {start: Math.min(target, pressEnd - 1)})
        }
        onReleased: Registry.endGesture()
        onCanceled: content.clipItem.cancelDrag()
    }

    MouseArea {
        id: endGrip
        objectName: "clip.resize:" + content.clipItem.clipId + ":right"
        anchors.right: parent.right
        width: Theme.clipGrip
        height: parent.height
        cursorShape: Qt.SizeHorCursor
        property real pressX
        property real pressEnd
        onPressed: mouse => {
            pressX = content.clipItem.sceneX(endGrip, mouse)
            pressEnd = content.clipItem.end
        }
        onPositionChanged: mouse => {
            const item = content.clipItem
            const target = item.place(pressEnd + (item.sceneX(endGrip, mouse) - pressX) / item.pixelsPerTick,
                                      mouse.modifiers)
            item.drag("clip.resize", {end: Math.max(target, item.start + 1)})
        }
        onReleased: Registry.endGesture()
        onCanceled: content.clipItem.cancelDrag()
    }

    FadeHandle {
        id: fadeInHandle
        clipItem: content.clipItem
        fadeOut: false
        shown: hover.hovered || content.clipItem.selected
        x: Math.min(content.clipItem.fadeIn * content.clipItem.pixelsPerTick, content.width - width)
    }

    FadeHandle {
        id: fadeOutHandle
        clipItem: content.clipItem
        fadeOut: true
        shown: hover.hovered || content.clipItem.selected
        x: Math.max(content.width - width - content.clipItem.fadeOut * content.clipItem.pixelsPerTick, 0)
    }
}
