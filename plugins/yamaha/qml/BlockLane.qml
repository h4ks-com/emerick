import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui

// A timeline lane of named blocks on its track. A click runs <actionPrefix>.edit, which opens the picker, a drag
// runs <actionPrefix>.move as one gesture, and a right click opens the block's context menu.
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
    property int focusedIndex: -1
    readonly property bool editing: Nav.editingBlock.track === trackId
    readonly property real editAt: editing ? Nav.editingBlock.at : 0
    readonly property string editLabel: blocks.find(block => block.at === editAt)?.label ?? ""

    objectName: actionPrefix + ".edit:" + trackId

    function ticksAt(x, modifiers) {
        return Math.max(0, Project.placed(Math.round(x / pixelsPerTick), modifiers))
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

    function deletes(event) {
        return event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete
    }

    function focusBlock(at) {
        for (let index = 0; index < blockRepeater.count; ++index) {
            const block = blockRepeater.itemAt(index)
            if (block && block.modelData.at === at) {
                block.forceActiveFocus()
            }
        }
    }

    onEditingChanged: editing ? pickerPopup.open() : pickerPopup.close()

    MouseArea {
        anchors.fill: parent
        onClicked: mouse => lane.run("edit", {at: lane.ticksAt(mouse.x, mouse.modifiers)})
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
        picked: [lane.blocks[lane.focusedIndex]?.at ?? -1, lane.editing ? lane.editAt : -1]
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

    // We count the blocks rather than list them, so a drag keeps its block item while the document follows it.
    Repeater {
        id: blockRepeater
        model: lane.blocks.length
        delegate: Item {
            id: block
            required property int index
            readonly property var modelData: lane.blocks[index] ?? {at: 0, end: 0, label: "", gap: true, colorIndex: 0}
            objectName: lane.actionPrefix + ".move:" + lane.trackId + ":" + modelData.at
            x: modelData.at * lane.pixelsPerTick
            y: Theme.space1
            width: Math.max(2 * Theme.hairline, (modelData.end - modelData.at) * lane.pixelsPerTick - Theme.hairline)
            height: lane.height - 2 * Theme.space1
            onActiveFocusChanged: {
                if (activeFocus) {
                    lane.focusedIndex = index
                } else if (lane.focusedIndex === index) {
                    lane.focusedIndex = -1
                }
            }
            Keys.onShortcutOverride: event => event.accepted = lane.deletes(event)
            Keys.onPressed: event => {
                if (lane.deletes(event)) {
                    event.accepted = true
                    const at = modelData.at
                    block.focus = false
                    lane.run("delete", {at: at})
                }
            }
            ToolTip.text: modelData.label + "\nClick to change, drag to move"
            ToolTip.visible: blockArea.containsMouse && !blockArea.tipDismissed
            ToolTip.delay: Theme.tooltipDelay

            MouseArea {
                id: blockArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor
                // A press hides the tooltip until the pointer leaves, so it never covers the blocks while we work.
                property bool tipDismissed: false
                property real pressX
                property real pressAt
                property bool dragging: false
                property bool context: false
                onExited: tipDismissed = false
                onPressed: mouse => {
                    tipDismissed = true
                    block.forceActiveFocus()
                    pressAt = block.modelData.at
                    dragging = false
                    context = Theme.isContextClick(mouse)
                    if (context) {
                        lane.menuAt = pressAt
                        lane.menuTo = lane.ticksAt(block.x + mouse.x, mouse.modifiers)
                        blockMenu.popup()
                        return
                    }
                    pressX = blockArea.mapToItem(null, mouse.x, 0).x
                }
                onPositionChanged: mouse => {
                    if (!pressed || context) {
                        return
                    }
                    const dx = blockArea.mapToItem(null, mouse.x, 0).x - pressX
                    dragging = dragging || Math.abs(dx) >= Theme.dragThreshold
                    if (dragging) {
                        Registry.updateGesture(Registry.actionInfo(lane.actionPrefix + ".move").title, [{
                            id: lane.actionPrefix + ".move",
                            params: {track: lane.trackId, at: pressAt, to: lane.ticksAt(pressAt * lane.pixelsPerTick + dx, mouse.modifiers)}
                        }])
                    }
                }
                onReleased: {
                    if (dragging) {
                        dragging = false
                        Registry.endGesture()
                    } else if (!context) {
                        lane.run("edit", {at: pressAt})
                    }
                }
                onCanceled: {
                    if (dragging) {
                        dragging = false
                        Registry.cancelGesture()
                        Registry.endGesture()
                    }
                }
            }
        }
    }

    Popup {
        id: pickerPopup
        objectName: lane.actionPrefix + ".picker:" + lane.trackId
        property real openedAt
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
        onAboutToShow: openedAt = lane.editAt
        onClosed: {
            if (lane.editing) {
                lane.closePicker()
            }
            lane.focusBlock(openedAt)
        }
    }
}
