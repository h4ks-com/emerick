import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui

// A timeline lane of named blocks on its track. A click on a block selects it, Shift extends the selection, a drag
// over empty space selects the blocks it touches and a drag of a selected block moves the selection as one gesture.
// A click on empty space or a double click on a block opens the picker, and a right click opens the block's menu.
Item {
    id: lane
    property string trackId
    property real pixelsPerTick: Nav.pixelsPerTick
    property string actionPrefix
    property string contextMenu
    property var blocks: []
    // The picker's content, which reads editAt and editLabel and calls set.
    property Component picker
    property real menuAt: 0
    property real menuTo: 0
    // The ticks in view, which the block strip draws.
    property real viewFrom: 0
    property real viewTo: width / Math.max(pixelsPerTick, Number.MIN_VALUE)
    // A press hides the tooltip until the pointer leaves the block, so it never covers the blocks while we work.
    property bool tipDismissed: false
    readonly property bool editing: Nav.editingBlock.track === trackId
    readonly property real editAt: editing ? Nav.editingBlock.at : 0
    readonly property string editLabel: blocks.find(block => block.at === editAt)?.label ?? ""
    readonly property var selection: Nav.selectedBlocks.track === trackId ? Nav.selectedBlocks : null

    objectName: actionPrefix + ".edit:" + trackId

    function ticksAt(x, modifiers) {
        return Math.max(0, Project.placed(Math.round(x / pixelsPerTick), modifiers))
    }

    function splitAt(modelData, x, modifiers) {
        const playhead = Project.positionTicks
        return playhead > modelData.at && playhead < modelData.end ? playhead : ticksAt(x, modifiers)
    }

    function run(action, params) {
        params.track = trackId
        Registry.runAction(actionPrefix + "." + action, params)
    }

    function set(params) {
        params.at = editAt
        run("set", params)
    }

    function closePicker() {
        Nav.editingBlock = {}
    }

    function deleteEdited() {
        const at = editAt
        closePicker()
        run("delete", {at: at})
    }

    function blockAt(x) {
        const ticks = x / pixelsPerTick
        return blocks.find(block => ticks >= block.at && ticks < block.end) ?? null
    }

    function selects(block) {
        return selection !== null && block.at >= selection.from && block.at < selection.to
    }

    function select(from, to) {
        run("select", {from: from, to: Math.max(to, from + 1)})
    }

    readonly property var keyActions: ({
        [Qt.Key_Delete]: () => ({action: "delete", params: {}}),
        [Qt.Key_Backspace]: () => ({action: "delete", params: {}}),
        [Qt.Key_Return]: () => ({action: "edit", params: {at: selection.from}}),
        [Qt.Key_Enter]: () => ({action: "edit", params: {at: selection.from}})
    })
    readonly property var commandActions: ({
        [Qt.Key_C]: () => ({action: "copy", params: {}}),
        [Qt.Key_D]: () => ({action: "duplicate", params: {}}),
        [Qt.Key_U]: () => ({action: "loop", params: {}})
    })
    function keyAction(event) {
        const command = event.modifiers & Qt.ControlModifier
        if (command && event.key === Qt.Key_V) {
            return {action: "paste", params: {}}
        }
        const action = (command ? commandActions : keyActions)[event.key]
        return action && selection !== null ? action() : null
    }
    Keys.onShortcutOverride: event => event.accepted = keyAction(event) !== null
    Keys.onPressed: event => {
        const action = keyAction(event)
        if (action) {
            event.accepted = true
            run(action.action, action.params)
        }
    }

    onEditingChanged: editing ? pickerPopup.open() : pickerPopup.close()

    MouseArea {
        id: laneArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: dragging && grabbed ? Qt.ClosedHandCursor : Qt.ArrowCursor
        property real pressX
        // The block the press landed on, and the selected range its drag moves.
        property var grabbed: null
        property var range: null
        property bool dragging: false
        property bool context: false
        onPressed: mouse => {
            lane.forceActiveFocus()
            lane.tipDismissed = true
            dragging = false
            pressX = mapToItem(null, mouse.x, 0).x
            grabbed = lane.blockAt(mouse.x)
            context = Theme.isContextClick(mouse)
            if (context) {
                if (!grabbed) {
                    context = false
                    mouse.accepted = false
                    return
                }
                if (!lane.selects(grabbed)) {
                    lane.select(grabbed.at, grabbed.at + 1)
                }
                lane.menuAt = grabbed.at
                lane.menuTo = lane.splitAt(grabbed, mouse.x, mouse.modifiers)
                blockMenu.popup()
                return
            }
            if (!grabbed) {
                band.begin(mouse.x, mouse.modifiers & Qt.ShiftModifier)
                return
            }
            if (mouse.modifiers & Qt.ShiftModifier && lane.selection) {
                lane.select(Math.min(lane.selection.from, grabbed.at), Math.max(lane.selection.to, grabbed.at + 1))
            } else if (!lane.selects(grabbed)) {
                lane.select(grabbed.at, grabbed.at + 1)
            }
            range = lane.selection ? {from: lane.selection.from, to: lane.selection.to} : null
        }
        onPositionChanged: mouse => {
            if (!pressed || context) {
                return
            }
            const dx = mapToItem(null, mouse.x, 0).x - pressX
            dragging = dragging || Math.abs(dx) >= Theme.dragThreshold
            if (!dragging) {
                return
            }
            if (!grabbed) {
                band.reach(mouse.x)
            } else if (range) {
                const shift = Math.max(-range.from,
                                       lane.ticksAt(grabbed.at * lane.pixelsPerTick + dx, mouse.modifiers) - grabbed.at)
                Registry.updateGesture(Registry.actionInfo(lane.actionPrefix + ".move").title, [{
                    id: lane.actionPrefix + ".move",
                    params: {track: lane.trackId, at: range.from, to: range.from + shift, end: range.to}
                }])
            }
        }
        onReleased: mouse => {
            if (context) {
                context = false
            } else if (dragging && grabbed) {
                Registry.endGesture()
            } else if (dragging) {
                band.end()
            } else if (grabbed) {
                if (!(mouse.modifiers & Qt.ShiftModifier)) {
                    lane.select(grabbed.at, grabbed.at + 1)
                }
            } else {
                lane.run("select", {})
                lane.run("edit", {at: lane.ticksAt(mouse.x, mouse.modifiers)})
            }
            dragging = false
        }
        onDoubleClicked: mouse => {
            const block = lane.blockAt(mouse.x)
            if (block) {
                lane.run("edit", {at: block.at})
            }
        }
        onCanceled: {
            if (dragging && grabbed) {
                Registry.cancelGesture()
                Registry.endGesture()
                lane.select(range.from, range.to)
            }
            band.visible = false
            dragging = false
            context = false
        }
    }

    ActionMenu {
        id: blockMenu
        objectName: "menu." + lane.actionPrefix + ".context"
        menuName: lane.contextMenu
        params: ({track: lane.trackId, at: lane.menuAt, to: lane.menuTo})
    }

    // We draw every block in view from one item, so a zoom or an edit repaints the lane once.
    BlockStrip {
        // The strip spans two views from a whole number of views in, so a scroll repaints it only past that span.
        readonly property real span: Math.max(1, lane.viewTo - lane.viewFrom)
        objectName: lane.actionPrefix + ".blocks:" + lane.trackId
        x: from * lane.pixelsPerTick
        width: 2 * span * lane.pixelsPerTick
        height: lane.height
        blocks: lane.blocks
        picked: lane.blocks.filter(block => lane.selects(block)).map(block => block.at)
                    .concat(lane.editing ? [lane.editAt] : [])
        from: Math.floor(lane.viewFrom / span) * span
        pixelsPerTick: lane.pixelsPerTick
        style: ({
            colors: Theme.trackColors,
            ink: Theme.ink,
            inkDim: Theme.inkDim,
            rule: Theme.rule,
            tape: Theme.tape,
            radius: Theme.radiusClip,
            inset: Theme.space1,
            hairline: Theme.hairline,
            selectionWidth: Theme.selectionWidth,
            pixelSize: Theme.textBody,
            minimumPixelSize: Theme.textSmall
        })
    }

    // The block items take no presses, so every press reaches the lane's mouse area and a drag keeps its press while
    // the blocks it passes reorder. We count the blocks rather than list them, so an edit keeps the items.
    Repeater {
        model: lane.blocks.length
        delegate: Item {
            required property int index
            readonly property var modelData: lane.blocks[index] ?? {at: 0, end: 0, label: ""}
            objectName: lane.actionPrefix + ".move:" + lane.trackId + ":" + modelData.at
            x: modelData.at * lane.pixelsPerTick
            y: Theme.space1
            width: Math.max(2 * Theme.hairline, (modelData.end - modelData.at) * lane.pixelsPerTick - Theme.hairline)
            height: lane.height - 2 * Theme.space1
            ToolTip.text: modelData.label + "\nClick to select, drag to move, double-click to change"
            ToolTip.visible: hover.hovered && !lane.tipDismissed
            ToolTip.delay: Theme.tooltipDelay

            HoverHandler {
                id: hover
                onHoveredChanged: {
                    if (!hovered) {
                        lane.tipDismissed = false
                    }
                }
            }
        }
    }

    // A drag over empty space selects the blocks it touches; Shift adds them to the selection.
    Rectangle {
        id: band
        objectName: lane.actionPrefix + ".band:" + lane.trackId
        property real fromX
        property real toX
        property bool adds
        visible: false
        x: Math.min(fromX, toX)
        width: Math.abs(toX - fromX)
        height: lane.height
        color: Theme.bandFill
        border.width: Theme.hairline
        border.color: Theme.tape

        function begin(at, add) {
            fromX = at
            toX = at
            adds = add
        }
        function reach(at) {
            toX = at
            visible = true
        }
        function end() {
            visible = false
            let from = Math.max(0, Math.round(x / lane.pixelsPerTick))
            let to = Math.round((x + width) / lane.pixelsPerTick)
            if (adds && lane.selection) {
                from = Math.min(from, lane.selection.from)
                to = Math.max(to, lane.selection.to)
            }
            lane.select(from, to)
        }
    }

    Popup {
        id: pickerPopup
        objectName: lane.actionPrefix + ".picker:" + lane.trackId
        x: lane.editAt * lane.pixelsPerTick
        y: lane.height
        margins: Theme.space2
        padding: Theme.space2
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GlassPanel {}
        contentItem: Loader {
            active: pickerPopup.visible
            sourceComponent: lane.picker
        }
        onClosed: {
            if (lane.editing) {
                lane.closePicker()
            }
            lane.forceActiveFocus()
        }
    }
}
