import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The piano roll's clip as notes over a keyboard, on the timeline's zoom and scroll, with a velocity lane below.
DockPanel {
    id: roll
    objectName: "pianoRoll"
    // A press outside the piano roll gives the arrow, Delete and edit keys back to the window.
    property bool editKeys: true

    readonly property var clip: Tracks.revision >= 0 && Nav.pianoRollClip !== "" ? Tracks.clip(Nav.pianoRollClip) : ({})
    readonly property bool hasClip: clip.type === "midi"
    readonly property var notes: hasClip ? clip.notes : []
    readonly property real clipStart: hasClip ? clip.start : 0
    readonly property real clipLength: hasClip ? clip.length : 0
    readonly property real pixelsPerTick: Nav.pixelsPerTick
    readonly property real scrollTicks: Nav.scrollTicks
    readonly property int keyHeight: Nav.keyHeight
    readonly property real gridTicks: 4 * Project.ticksPerQuarter / Registry.settings["midi.grid"]
    readonly property int keysTop: Theme.rulerHeight
    readonly property real gridWidth: Math.max(0, width - Theme.trackHeaderWidth)
    readonly property real velocityHeight: Math.min(Theme.velocityLaneHeight, height * Theme.velocityLaneShare)
    readonly property real gridHeight: Math.max(0, height - keysTop - velocityHeight)
    readonly property real contentHeight: 128 * keyHeight
    readonly property int barStep: {
        let step = 1
        while (step * Project.ticksPerBar * pixelsPerTick < Theme.labelSpacing) {
            step *= 2
        }
        return step
    }
    readonly property bool showBeats: barStep === 1 && Project.ticksPerBeat * pixelsPerTick >= Theme.beatSpacing
    // Key rows from the top of the keys to the middle of the view, so a resize or key zoom keeps the middle in place.
    property real middleRow: 127.5 - 60
    readonly property real scrollY: clampedScroll(middleRow * keyHeight - gridHeight / 2)

    // Grid x of a position in ticks from the clip start, and back.
    function xOf(ticks) {
        return (clipStart + ticks - scrollTicks) * pixelsPerTick
    }
    function ticksAt(x) {
        return scrollTicks + x / pixelsPerTick - clipStart
    }
    function yOf(pitch) {
        return (127 - pitch) * keyHeight - scrollY
    }
    function pitchAt(y) {
        return Math.max(0, Math.min(127, 127 - Math.floor((y + scrollY) / keyHeight)))
    }
    // Snaps a position in ticks from the clip start to the grid, which counts from bar 1.
    function place(ticks, modifiers) {
        return Project.placedOn(Math.round(clipStart + ticks), modifiers, gridTicks) - clipStart
    }
    function clampedScroll(y) {
        return Math.max(0, Math.min(y, contentHeight - gridHeight))
    }
    function scrollKeys(dy) {
        middleRow = (clampedScroll(scrollY + dy) + gridHeight / 2) / keyHeight
    }
    function showNotes() {
        const pitches = notes.length > 0 ? notes.map(note => note.pitch) : [60]
        const low = Math.min(...pitches)
        const high = Math.max(...pitches)
        const span = high - low + 1
        if (gridHeight > 0 && span * keyHeight > gridHeight) {
            Nav.keyHeight = Math.floor(gridHeight / span)
        }
        middleRow = 127.5 - (low + high) / 2
    }
    function noteName(pitch) {
        return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][pitch % 12] + (Math.floor(pitch / 12) - 2)
    }
    function isBlack(pitch) {
        return [1, 3, 6, 8, 10].indexOf(pitch % 12) >= 0
    }
    function selected(id) {
        return Nav.selectedNotes.indexOf(id) >= 0
    }
    function noteAt(x, y) {
        const pitch = pitchAt(y)
        const ticks = ticksAt(x)
        for (let index = notes.length - 1; index >= 0; --index) {
            const note = notes[index]
            if (note.pitch === pitch && ticks >= note.start && ticks < note.start + note.length) {
                return note
            }
        }
        return null
    }
    function selectOnPress(note, modifiers) {
        if (modifiers & Qt.ShiftModifier) {
            const ids = Array.from(Nav.selectedNotes)
            const at = ids.indexOf(note.id)
            if (at >= 0) {
                ids.splice(at, 1)
            } else {
                ids.push(note.id)
            }
            Registry.runAction("midi.selectNotes", {notes: ids})
        } else if (!selected(note.id)) {
            Registry.runAction("midi.selectNotes", {notes: [note.id]})
        }
    }
    function zoomAround(factor, x) {
        const anchor = scrollTicks + (x - Theme.trackHeaderWidth) / pixelsPerTick
        Nav.pixelsPerTick = pixelsPerTick * factor
        Nav.scrollTicks = Math.max(0, anchor - (x - Theme.trackHeaderWidth) / Nav.pixelsPerTick)
    }
    function zoomKeys(height) {
        middleRow = (scrollY + gridHeight / 2) / keyHeight
        Nav.keyHeight = height
    }
    readonly property string clipId: Nav.pianoRollClip
    onClipIdChanged: Qt.callLater(showNotes)
    Component.onCompleted: Qt.callLater(showNotes)

    readonly property var keyActions: ({
        [Qt.Key_Up]: event => ({id: "midi.moveNotes", params: {pitch: event.modifiers & Qt.ShiftModifier ? 12 : 1}}),
        [Qt.Key_Down]: event => ({id: "midi.moveNotes", params: {pitch: event.modifiers & Qt.ShiftModifier ? -12 : -1}}),
        [Qt.Key_Left]: () => ({id: "midi.moveNotes", params: {by: -roll.gridTicks}}),
        [Qt.Key_Right]: () => ({id: "midi.moveNotes", params: {by: roll.gridTicks}}),
        [Qt.Key_Delete]: () => ({id: "midi.deleteNotes", params: {}}),
        [Qt.Key_Backspace]: () => ({id: "midi.deleteNotes", params: {}})
    })
    readonly property var commandActions: ({
        [Qt.Key_C]: () => ({id: "midi.copyNotes", params: {}}),
        [Qt.Key_V]: () => ({id: "midi.pasteNotes", params: {}}),
        [Qt.Key_A]: () => ({id: "midi.selectNotes", params: {all: true}})
    })
    function keyAction(event) {
        if (!hasClip) {
            return null
        }
        const table = event.modifiers & Qt.ControlModifier ? commandActions : keyActions
        const action = table[event.key]
        return action ? action(event) : null
    }
    Keys.onShortcutOverride: event => event.accepted = keyAction(event) !== null
    Keys.onPressed: event => {
        const action = keyAction(event)
        if (action) {
            event.accepted = true
            Registry.runAction(action.id, action.params)
        }
    }

    // We use a MouseArea because a WheelHandler accepts one orientation and drops a tilt wheel's horizontal events.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: event => {
            const dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x * Theme.wheelStep
            const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y * Theme.wheelStep
            if (event.modifiers & Qt.AltModifier) {
                roll.zoomKeys(Nav.keyHeight + Math.round(dy * Theme.wheelTrackHeight))
                roll.zoomAround(Math.exp(event.angleDelta.x * Theme.wheelZoom), event.x)
            } else if (event.modifiers & Qt.ControlModifier) {
                roll.zoomAround(Math.exp(event.angleDelta.y * Theme.wheelZoom), event.x)
            } else if (event.modifiers & Qt.ShiftModifier) {
                Nav.scrollTicks = Math.max(0, roll.scrollTicks - (dy + dx) / roll.pixelsPerTick)
            } else {
                Nav.scrollTicks = Math.max(0, roll.scrollTicks - dx / roll.pixelsPerTick)
                roll.scrollKeys(-dy)
            }
        }
    }

    Item {
        id: grid
        objectName: "pianoRoll.grid"
        x: Theme.trackHeaderWidth
        y: roll.keysTop
        width: roll.gridWidth
        height: roll.gridHeight
        clip: true

        PianoGrid {
            anchors.fill: parent
            roll: roll
        }

        // We count the notes rather than list them, so an edit keeps the note items.
        Repeater {
            model: roll.notes.length
            delegate: Rectangle {
                required property int index
                readonly property var note: roll.notes[index] ?? {id: "", pitch: 0, velocity: 1, start: 0, length: 0}
                objectName: "note:" + note.id
                x: roll.xOf(note.start)
                y: roll.yOf(note.pitch)
                width: Math.max(Theme.velocityBar, note.length * roll.pixelsPerTick)
                height: roll.keyHeight
                visible: x + width > 0 && x < grid.width && y + height > 0 && y < grid.height
                radius: Math.min(Theme.radiusClip / 2, height / 2)
                readonly property bool chosen: roll.selected(note.id)
                color: chosen ? Theme.selectedNote(note.velocity) : Theme.velocityColor(note.velocity)
                border.width: chosen ? Theme.selectionWidth : Theme.hairline
                border.color: chosen ? Theme.ink : Theme.clipInk
                Item {
                    objectName: "midi.resizeNotes:" + parent.note.id
                    anchors.right: parent.right
                    width: Math.min(Theme.noteGrip, parent.width / 2)
                    height: parent.height
                }
            }
        }

        Rectangle {
            id: band
            visible: false
            color: Theme.bandFill
            border.width: Theme.hairline
            border.color: Theme.tape
        }

        Rectangle {
            x: (Project.positionTicks - roll.scrollTicks) * roll.pixelsPerTick
            width: Theme.hairline
            height: grid.height
            visible: x >= 0 && x <= grid.width
            color: Theme.tape
        }

        MouseArea {
            id: edit
            objectName: "midi.addNote"
            anchors.fill: parent
            enabled: roll.hasClip
            property string mode: ""
            property point pressAt
            property var pressNote: null
            property var pressSelection: []
            readonly property var heldNote: (mode === "move" || mode === "resize") && pressNote
                                            ? roll.notes.find(note => note.id === pressNote.id) ?? null : null
            ToolTip.visible: heldNote !== null
            ToolTip.text: heldNote ? roll.noteName(heldNote.pitch) + "   " + Project.ticksText(roll.clipStart + heldNote.start, false)
                                     + "   " + Project.ticksText(heldNote.length, true) : ""
            cursorShape: mode === "resize" || (containsMouse && hoverResize) ? Qt.SizeHorCursor : Qt.ArrowCursor
            hoverEnabled: true
            readonly property bool hoverResize: {
                const note = containsMouse ? roll.noteAt(mouseX, mouseY) : null
                return note !== null && mouseX >= roll.xOf(note.start + note.length) - Theme.noteGrip
            }

            function addNote(mouse) {
                const free = mouse.modifiers & ~Qt.ControlModifier
                const start = roll.place(roll.ticksAt(mouse.x) - roll.gridTicks / 2, free)
                if (start < 0 || start >= roll.clipLength) {
                    return null
                }
                const added = Registry.runAction("midi.addNote", {pitch: roll.pitchAt(mouse.y), start: Math.max(0, start)})
                return added ? added.notes[0] : null
            }

            onPressed: mouse => {
                roll.forceActiveFocus()
                pressAt = Qt.point(mouse.x, mouse.y)
                pressNote = roll.noteAt(mouse.x, mouse.y)
                if (pressNote) {
                    roll.selectOnPress(pressNote, mouse.modifiers)
                    keys.play(pressNote.pitch)
                    mode = mouse.x >= roll.xOf(pressNote.start + pressNote.length) - Theme.noteGrip ? "resize" : "move"
                    return
                }
                if (Registry.settings["midi.pencil"] === true || (mouse.modifiers & Qt.ControlModifier)) {
                    pressNote = addNote(mouse)
                    mode = pressNote ? "resize" : ""
                    return
                }
                pressSelection = mouse.modifiers & Qt.ShiftModifier ? Array.from(Nav.selectedNotes) : []
                mode = "band"
            }
            onPositionChanged: mouse => {
                if (!pressed || mode === "") {
                    return
                }
                const dx = mouse.x - pressAt.x
                if (mode === "band") {
                    band.x = Math.min(pressAt.x, mouse.x)
                    band.y = Math.min(pressAt.y, mouse.y)
                    band.width = Math.abs(dx)
                    band.height = Math.abs(mouse.y - pressAt.y)
                    band.visible = true
                    return
                }
                if (mode === "move") {
                    if (Math.abs(dx) < Theme.dragThreshold && Math.abs(mouse.y - pressAt.y) < Theme.dragThreshold) {
                        return
                    }
                    const by = roll.place(pressNote.start + dx / roll.pixelsPerTick, mouse.modifiers) - pressNote.start
                    const pitch = roll.pitchAt(mouse.y) - roll.pitchAt(pressAt.y)
                    Registry.updateGesture(Registry.actionInfo("midi.moveNotes").title, [{id: "midi.moveNotes",
                        params: {by: by, pitch: pitch}}])
                    keys.play(Math.max(0, Math.min(127, pressNote.pitch + pitch)))
                    return
                }
                const end = pressNote.start + pressNote.length
                const target = Math.max(pressNote.start + 1, roll.place(end + dx / roll.pixelsPerTick, mouse.modifiers))
                Registry.updateGesture(Registry.actionInfo("midi.resizeNotes").title,
                                       [{id: "midi.resizeNotes", params: {by: target - end}}])
            }
            onReleased: {
                if (mode === "band") {
                    const from = roll.ticksAt(band.x)
                    const to = roll.ticksAt(band.x + band.width)
                    const high = roll.pitchAt(band.y)
                    const low = roll.pitchAt(band.y + band.height)
                    const inside = band.visible ? roll.notes.filter(note => note.pitch >= low && note.pitch <= high
                                                                    && note.start < to && note.start + note.length > from)
                                                : []
                    const ids = pressSelection.concat(inside.map(note => note.id).filter(id => pressSelection.indexOf(id) < 0))
                    Registry.runAction("midi.selectNotes", {notes: ids})
                    band.visible = false
                } else if (mode !== "") {
                    Registry.endGesture()
                }
                keys.play(-1)
                mode = ""
            }
            onCanceled: {
                band.visible = false
                if (mode === "move" || mode === "resize") {
                    Registry.cancelGesture()
                    Registry.endGesture()
                }
                keys.play(-1)
                mode = ""
            }
        }
    }

    PianoKeyboard {
        id: keys
        y: roll.keysTop
        width: Theme.trackHeaderWidth
        height: roll.gridHeight
        roll: roll
    }

    VelocityLane {
        y: roll.keysTop + roll.gridHeight
        width: roll.width
        height: roll.velocityHeight
        roll: roll
    }

    Rectangle {
        width: Theme.trackHeaderWidth
        height: roll.keysTop
        color: Theme.slate
        Text {
            anchors.left: parent.left
            anchors.right: tools.left
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: Theme.space3
            text: roll.hasClip ? roll.clip.trackName + "   Take " + roll.clip.take : ""
            color: Theme.ink
            font.pixelSize: Theme.textSmall
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Row {
            id: tools
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            ChromeButton {
                objectName: "midi.togglePencil"
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
                iconName: "pencil-simple"
                iconColor: Theme.inkDim
                active: Registry.settings["midi.pencil"] === true
                activeColor: Theme.ink
                hint: "Hold " + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + " and click to add a note with either tool"
            }
        }
    }

    Ruler {
        x: Theme.trackHeaderWidth
        width: roll.gridWidth
        height: Theme.rulerHeight
        scrollTicks: roll.scrollTicks
        pixelsPerTick: roll.pixelsPerTick
        barStep: roll.barStep
        showBeats: roll.showBeats
    }

    Rectangle {
        x: Theme.trackHeaderWidth - Theme.hairline
        width: Theme.hairline
        height: roll.height
        color: Theme.rule
    }

    Label {
        anchors.centerIn: grid
        visible: !roll.hasClip
        text: "Double-click a MIDI clip to edit its notes"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
}
