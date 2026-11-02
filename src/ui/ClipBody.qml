import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Shapes
import Emerick.App

Item {
    id: content

    required property ClipItem clipItem
    // We create only the repeats in view, so a short clip looped for many bars stays light.
    readonly property real loopPeriod: clipItem.looped ? clipItem.clip.length : 0
    readonly property int firstRepeat: loopPeriod > 0
                                       ? Math.max(1, Math.floor((clipItem.viewFrom - clipItem.start) / loopPeriod)) : 0
    readonly property int lastRepeat: loopPeriod > 0
                                      ? Math.min(Math.ceil((clipItem.end - clipItem.start) / loopPeriod) - 1,
                                                 Math.floor((clipItem.viewTo - clipItem.start) / loopPeriod))
                                      : -1

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
        visible: clipItem.audio && (shown || pressed) && !Registry.readOnly

        MouseArea {
            id: grab
            objectName: "clip.setFade:" + handle.clipItem.clipId + (handle.fadeOut ? ":out" : ":in")
            anchors.fill: parent
            anchors.margins: -Theme.space1
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: axis === "y" ? Qt.SizeVerCursor : Qt.SizeHorCursor
            readonly property string edge: handle.fadeOut ? "fadeOut" : "fadeIn"
            property point pressAt
            property real pressFade
            property real pressMidpoint
            property string axis: ""
            onPressed: mouse => {
                const item = handle.clipItem
                if (Theme.isContextClick(mouse)) {
                    if (!item.selected) {
                        item.selectOnPress(Qt.NoModifier)
                    }
                    item.timeline.openClipMenu()
                    axis = "menu"
                    return
                }
                pressAt = grab.mapToItem(null, mouse.x, mouse.y)
                pressFade = item[edge]
                const points = Project.fadeCurve(item[edge + "Shape"], item[edge + "Curve"])
                pressMidpoint = points[points.length >> 1].y
                axis = ""
            }
            onCanceled: handle.clipItem.cancelDrag()
            onPositionChanged: mouse => {
                const item = handle.clipItem
                if (axis === "menu") {
                    return
                }
                const at = grab.mapToItem(null, mouse.x, mouse.y)
                const dx = at.x - pressAt.x
                const dy = at.y - pressAt.y
                if (axis === "") {
                    if (Math.max(Math.abs(dx), Math.abs(dy)) < Theme.dragThreshold) {
                        return
                    }
                    axis = Math.abs(dy) > Math.abs(dx) ? "y" : "x"
                }
                if (axis === "y") {
                    const gain = pressMidpoint - dy / content.height
                    item.drag("clip.setFade", {[edge + "Curve"]: Project.curveThroughMidpoint(item[edge + "Shape"], gain)})
                    return
                }
                const ticks = dx / item.pixelsPerTick
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
        value: move.pressed || startGrip.pressed || endGrip.pressed || loopGrip.pressed || fadeInHandle.pressed
               || fadeOutHandle.pressed
    }

    HoverHandler {
        id: hover
    }

    // A drag shows where the clip lands, as Logic's help tag does.
    ToolTip.visible: move.dragging || startGrip.pressed || endGrip.pressed || loopGrip.pressed
    ToolTip.text: move.dragging ? Registry.actionInfo("clip.move").title + "   " + Project.ticksText(clipItem.start, false)
                : startGrip.pressed ? Registry.actionInfo("clip.resize").title + "   " + Project.ticksText(clipItem.start, false)
                : loopGrip.pressed ? Registry.actionInfo("clip.setLoopLength").title + "   " + Project.ticksText(clipItem.end, false)
                : Registry.actionInfo("clip.resize").title + "   " + Project.ticksText(clipItem.contentEnd, false)

    Rectangle {
        id: body
        anchors.fill: parent
        anchors.topMargin: Theme.clipInset
        anchors.bottomMargin: Theme.clipInset + Theme.hairline
        radius: Theme.radiusClip
        color: content.clipItem.clipMuted ? Theme.mutedClip : content.clipItem.trackColor
        clip: true

        // We tint the label opaque, because its square bottom half overlaps its rounded top and would darken twice.
        Rectangle {
            id: label
            width: parent.width
            height: Theme.clipLabelHeight
            radius: Theme.radiusClip
            color: Qt.tint(body.color, Theme.clipLabel)
            // The name stays at the left edge of the view while the clip starts before it, as in Logic.
            readonly property real leftEdge: Theme.fadeHandle + Theme.space1
                                             + Math.max(0, (content.clipItem.viewFrom - content.clipItem.start) * content.clipItem.pixelsPerTick)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.radius
                color: label.color
            }
            IconImage {
                id: mutedIcon
                objectName: "clip.muted:" + content.clipItem.clipId
                visible: content.clipItem.clipMuted
                x: label.leftEdge
                anchors.verticalCenter: parent.verticalCenter
                source: Theme.iconSource("speaker-slash")
                sourceSize: Qt.size(Theme.clipLabelHeight - Theme.space1, Theme.clipLabelHeight - Theme.space1)
                color: Theme.ink
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: label.leftEdge + (mutedIcon.visible ? mutedIcon.width + Theme.space1 : 0)
                anchors.rightMargin: Theme.space1
                verticalAlignment: Text.AlignVCenter
                objectName: "clip.name:" + content.clipItem.clipId
                visible: !rename.visible
                text: content.clipItem.clip.name || content.clipItem.trackName + "   Take " + content.clipItem.clip.take
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
                    textureSize: Theme.notesTexture(width, height)
                }
            }
        }

        Rectangle {
            visible: content.clipItem.looped
            x: (content.clipItem.contentEnd - content.clipItem.start) * content.clipItem.pixelsPerTick
            width: parent.width - x
            height: parent.height
            color: Theme.loopWash
        }

        Repeater {
            model: Math.max(0, content.lastRepeat - content.firstRepeat + 1)
            Item {
                required property int index
                readonly property real offset: (content.firstRepeat + index) * content.loopPeriod
                objectName: "clip.repeat:" + content.clipItem.clipId + ":" + (content.firstRepeat + index)
                x: offset * content.clipItem.pixelsPerTick
                width: Math.min(content.loopPeriod, content.clipItem.end - content.clipItem.start - offset)
                       * content.clipItem.pixelsPerTick
                height: parent.height

                Item {
                    y: Theme.clipLabelHeight
                    width: parent.width
                    height: parent.height - Theme.clipLabelHeight
                    clip: true
                    ClipNotes {
                        width: content.loopPeriod * content.clipItem.pixelsPerTick
                        height: parent.height
                        notes: content.clipItem.clip.notes
                        length: content.loopPeriod
                        color: Theme.clipInk
                        textureSize: Theme.notesTexture(width, height)
                    }
                }
                Rectangle {
                    x: -Theme.loopNotch / 2
                    width: Theme.loopNotch
                    height: Theme.loopNotch
                    color: Theme.graphite
                }
                Rectangle {
                    x: -Theme.loopNotch / 2
                    y: parent.height - Theme.loopNotch
                    width: Theme.loopNotch
                    height: Theme.loopNotch
                    color: Theme.graphite
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: content.clipItem.fadeIn > 0 || content.clipItem.fadeOut > 0
            sourceComponent: Shape {
                readonly property real inWidth: content.clipItem.fadeIn * content.clipItem.pixelsPerTick
                readonly property real outWidth: content.clipItem.fadeOut * content.clipItem.pixelsPerTick
                ShapePath {
                    fillColor: Theme.fadeShade
                    strokeColor: Theme.fadeLine
                    strokeWidth: Theme.hairline
                    PathPolyline {
                        path: [Qt.point(0, 0)].concat(Project.fadeCurve(content.clipItem.fadeInShape, content.clipItem.fadeInCurve)
                                                      .map(p => Qt.point(p.x * inWidth, (1 - p.y) * body.height)))
                    }
                }
                ShapePath {
                    fillColor: Theme.fadeShade
                    strokeColor: Theme.fadeLine
                    strokeWidth: Theme.hairline
                    PathPolyline {
                        path: [Qt.point(body.width, 0)].concat(Project.fadeCurve(content.clipItem.fadeOutShape, content.clipItem.fadeOutCurve)
                                                               .map(p => Qt.point(body.width - p.x * outWidth, (1 - p.y) * body.height)))
                    }
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

    TextField {
        id: rename
        objectName: "clip.rename:" + content.clipItem.clipId
        z: 1
        x: label.leftEdge
        y: Theme.clipInset
        width: Math.max(Theme.labelWidth, parent.width - x - Theme.space1)
        height: Theme.clipLabelHeight
        visible: Nav.renamingClip !== "" && Nav.renamingClip === content.clipItem.clipId
        ToolTip.text: Registry.actionInfo("clip.rename").title
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
                text = content.clipItem.clip.name
                forceActiveFocus()
                selectAll()
            } else {
                focus = false
            }
        }
        onAccepted: {
            if (text !== content.clipItem.clip.name) {
                Registry.runAction("clip.rename", {clip: content.clipItem.clipId, name: text})
            }
            Nav.renamingClip = ""
        }
        onActiveFocusChanged: if (!activeFocus && visible) Nav.renamingClip = ""
        Keys.onEscapePressed: Nav.renamingClip = ""
    }

    MouseArea {
        id: move
        objectName: "clip.move:" + content.clipItem.clipId
        enabled: !Registry.readOnly
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
                    item.selectOnPress(Qt.NoModifier)
                }
                item.timeline.openClipMenu()
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
        onDoubleClicked: {
            if (!content.clipItem.audio) {
                Registry.runAction("midi.editClip", {clip: content.clipItem.clipId})
            }
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
            if (accepted && track !== "") {
                Registry.runAction("track.select", {track: track})
            }
        }
    }

    MouseArea {
        id: startGrip
        objectName: "clip.resize:" + content.clipItem.clipId + ":left"
        visible: !Registry.readOnly
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
        Rectangle {
            anchors.centerIn: parent
            width: 2 * Theme.hairline
            height: Math.min(parent.height - 2 * Theme.space1, Theme.clipLabelHeight)
            radius: Theme.hairline
            color: Theme.ink
            visible: hover.hovered || parent.pressed
        }
    }

    // A MIDI clip's right edge resizes its content below the middle and drags its loop above it, as in Logic. A looped
    // clip keeps the resize edge at the end of its content.
    MouseArea {
        id: endGrip
        objectName: "clip.resize:" + content.clipItem.clipId + ":right"
        visible: !Registry.readOnly
        x: (content.clipItem.contentEnd - content.clipItem.start) * content.clipItem.pixelsPerTick - width
        y: content.clipItem.audio ? 0 : parent.height / 2
        width: Theme.clipGrip
        height: content.clipItem.audio ? parent.height : parent.height / 2
        cursorShape: Qt.SizeHorCursor
        property real pressX
        property real pressEnd
        onPressed: mouse => {
            pressX = content.clipItem.sceneX(endGrip, mouse)
            pressEnd = content.clipItem.contentEnd
        }
        onPositionChanged: mouse => {
            const item = content.clipItem
            const target = item.place(pressEnd + (item.sceneX(endGrip, mouse) - pressX) / item.pixelsPerTick,
                                      mouse.modifiers)
            item.drag("clip.resize", {end: Math.max(target, item.start + 1)})
        }
        onReleased: Registry.endGesture()
        onCanceled: content.clipItem.cancelDrag()
        Rectangle {
            anchors.centerIn: parent
            width: 2 * Theme.hairline
            height: Math.min(parent.height - 2 * Theme.space1, Theme.clipLabelHeight)
            radius: Theme.hairline
            color: Theme.ink
            visible: hover.hovered || parent.pressed
        }
    }

    MouseArea {
        id: loopGrip
        objectName: "clip.setLoopLength:" + content.clipItem.clipId
        visible: !content.clipItem.audio && !Registry.readOnly
        anchors.right: parent.right
        width: Theme.clipGrip
        height: parent.height / 2
        cursorShape: Qt.SizeHorCursor
        property real pressX
        property real pressEnd
        onPressed: mouse => {
            pressX = content.clipItem.sceneX(loopGrip, mouse)
            pressEnd = content.clipItem.end
        }
        onPositionChanged: mouse => {
            const item = content.clipItem
            const target = item.place(pressEnd + (item.sceneX(loopGrip, mouse) - pressX) / item.pixelsPerTick,
                                      mouse.modifiers)
            item.drag("clip.setLoopLength", {end: Math.max(target, item.start + 1)})
        }
        onReleased: Registry.endGesture()
        onCanceled: content.clipItem.cancelDrag()

        IconImage {
            visible: hover.hovered || loopGrip.pressed
            anchors.right: parent.right
            y: Theme.clipInset
            source: Theme.iconSource("repeat")
            sourceSize: Qt.size(Theme.clipLabelHeight - Theme.space1, Theme.clipLabelHeight - Theme.space1)
            color: Theme.ink
        }
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
