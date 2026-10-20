import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The one number field every view uses, after Blender's: drag sideways to change the value, Shift drags finely and
// Ctrl snaps to coarse steps, the arrows step it, and a click opens it for typing. Enter commits, Escape cancels.
// Inside a view with a DragGroup, dragging up or down first gathers the fields it crosses and the sideways drag moves
// them all by the same number of steps. A drag runs its actions live as one registry gesture, so every view of the value
// follows it and it undoes in one step; Escape reverts it.
Item {
    id: field

    property real value
    property real from: -Number.MAX_VALUE
    property real to: Number.MAX_VALUE
    property real step: 1
    property int decimals: 0
    property string unit
    // Text after the value, such as "3 clips" when the edit applies to every selected clip.
    property string badge
    // The registry action the field runs; its title is the tooltip.
    property string actionId
    property string param
    // Returns the registry calls, {id, params}, that set a number. Without it the field emits committed.
    property var actionsFor
    property bool framed: true
    property alias font: label.font
    property color textColor: Theme.ink
    property var textFromValue: number => number.toFixed(decimals) + (unit ? " " + unit : "")
    // Returns NaN for text it cannot read. A leading + or - steps from the current value, and a/b divides.
    property var valueFromText: text => readNumber(text)

    // A drag holds the field, and gathered marks it as one of several moving together. A field without actionsFor shows
    // dragValue until it emits committed on release.
    property bool held: false
    property bool gathered: false
    property real dragStart
    property real dragValue

    readonly property bool dragging: drag.dragging
    readonly property bool editing: editor.visible
    readonly property bool editScope: true
    readonly property real margin: framed ? Theme.space4 : 0
    readonly property real shown: held && !actionsFor ? dragValue : value
    readonly property string displayText: textFromValue(shown) + (badge ? "   " + badge : "")

    signal committed(real value)

    implicitWidth: Theme.labelWidth
    implicitHeight: Theme.buttonHeight

    function clamped(number) {
        const factor = Math.pow(10, decimals)
        return Math.min(to, Math.max(from, Math.round(number * factor) / factor))
    }

    function readNumber(text) {
        const trimmed = text.replace(unit, "").trim()
        const relative = /^[+-]/.test(trimmed) && trimmed.length > 1
        const parts = trimmed.replace(/^\+/, "").split("/")
        const number = parts.length === 2 ? Number(parts[0]) / Number(parts[1]) : Number(parts[0])
        return relative ? value + number : number
    }

    function stepped(steps, coarse) {
        const raw = dragStart + steps * step
        const coarseStep = step * Theme.dragCoarseSteps
        return clamped(coarse ? Math.round(raw / coarseStep) * coarseStep : raw)
    }

    function hold(asGathered) {
        if (!held) {
            dragStart = value
            dragValue = value
            held = true
        }
        gathered = asGathered
    }

    function letGo() {
        held = false
        gathered = false
    }

    // The calls of every entry, {field, value}. Calls of one action that set different params of the same target merge
    // into one call, so fade in and fade out of a clip set both fades at once.
    function callsFor(entries) {
        const calls = []
        for (const entry of entries) {
            for (const call of entry.field.actionsFor(entry.value)) {
                const same = calls.find(other => other.id === call.id && Object.keys(call.params).every(
                                            key => !(key in other.params) || other.params[key] === call.params[key]))
                if (same) {
                    Object.assign(same.params, call.params)
                } else {
                    calls.push({id: call.id, params: Object.assign({}, call.params)})
                }
            }
        }
        return calls
    }

    function undoName(calls) {
        return calls.length > 0 ? Registry.actionInfo(calls[0].id).title : ""
    }

    function commit(number) {
        if (isNaN(number) || clamped(number) === value) {
            return
        }
        if (!actionsFor) {
            committed(clamped(number))
            return
        }
        const calls = callsFor([{field: field, value: clamped(number)}])
        if (calls.length === 1) {
            Registry.runAction(calls[0].id, calls[0].params)
        } else if (calls.length > 1) {
            Registry.runActions(undoName(calls), calls)
        }
    }

    function dragTo(fields) {
        const live = fields.filter(other => other.actionsFor)
        if (live.length === 0) {
            return
        }
        const calls = callsFor(live.filter(other => other.dragValue !== other.dragStart)
                               .map(other => ({field: other, value: other.dragValue})))
        Registry.updateGesture(undoName(calls), calls)
    }

    function stepBy(direction) {
        const current = editing ? valueFromText(editor.text) : value
        const number = clamped((isNaN(current) ? value : current) + direction * step)
        commit(number)
        if (editing) {
            editor.text = textFromValue(number)
            editor.selectAll()
        }
    }

    function startEditing() {
        editor.text = textFromValue(value)
        editor.visible = true
        editor.forceActiveFocus()
        editor.selectAll()
    }

    function dragGroup() {
        for (let item = parent; item; item = item.parent) {
            if (item.dragGroup) {
                return item.dragGroup
            }
        }
        return null
    }

    ToolTip.text: actionId ? Registry.actionInfo(actionId).title : ""
    ToolTip.visible: actionId !== "" && hover.hovered && !held && !editing
    ToolTip.delay: Theme.tooltipDelay

    HoverHandler {
        id: hover
        cursorShape: editing ? Qt.IBeamCursor : Qt.SizeHorCursor
    }

    Rectangle {
        anchors.fill: parent
        visible: field.framed || field.gathered
        radius: Theme.radiusClip
        color: field.gathered ? Theme.inkDim : field.held || hover.hovered ? Theme.rule : Theme.lift
    }

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: field.margin
        anchors.rightMargin: field.margin
        visible: !field.editing
        text: field.displayText
        color: !field.enabled ? Theme.inkDim : field.gathered ? Theme.graphite : field.textColor
        font.pixelSize: Theme.textLabel
        font.features: {"tnum": 1}
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    MouseArea {
        id: drag
        objectName: field.objectName + ":drag"
        anchors.fill: parent
        enabled: !field.editing
        preventStealing: true
        property point press
        property bool gathering: false
        property bool dragging: false
        property var fields: []

        function holdOnly(list) {
            for (const other of fields) {
                if (list.indexOf(other) < 0) {
                    other.letGo()
                }
            }
            for (const other of list) {
                other.hold(gathering)
            }
            fields = list
        }

        function finish() {
            const entries = dragging ? fields.filter(other => other.dragValue !== other.dragStart)
                                               .map(other => ({field: other, value: other.dragValue})) : []
            holdOnly([])
            gathering = false
            dragging = false
            return entries
        }

        onPressed: mouse => {
            press = mapToItem(null, mouse.x, mouse.y)
            gathering = false
            dragging = false
        }
        onPositionChanged: mouse => {
            const at = mapToItem(null, mouse.x, mouse.y)
            const dx = at.x - press.x
            const sideways = Math.abs(dx) >= Theme.dragThreshold
            const group = field.dragGroup()
            if (!dragging && !gathering && !sideways && group && Math.abs(at.y - press.y) >= Theme.dragThreshold) {
                gathering = true
            }
            if (!dragging && sideways) {
                dragging = true
                holdOnly(gathering ? fields : [field])
            } else if (gathering && !dragging) {
                holdOnly(group.fieldsCrossing(press, at))
            }
            if (!dragging) {
                return
            }
            const steps = dx / Theme.dragPixelsPerStep * (mouse.modifiers & Qt.ShiftModifier ? Theme.dragFineFactor : 1)
            for (const other of fields) {
                other.dragValue = other.stepped(steps, mouse.modifiers & Qt.ControlModifier)
            }
            field.dragTo(fields)
        }
        onReleased: {
            const clicked = !dragging && !gathering
            const entries = finish()
            if (clicked) {
                field.startEditing()
            }
            for (const entry of entries.filter(entry => !entry.field.actionsFor)) {
                entry.field.committed(entry.value)
            }
            Registry.endGesture()
        }
        onCanceled: {
            finish()
            Registry.cancelGesture()
            Registry.endGesture()
        }
    }

    TextInput {
        id: editor
        objectName: field.objectName + ":text"
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space4
        visible: false
        color: field.textColor
        selectionColor: Theme.rule
        selectedTextColor: Theme.ink
        font: label.font
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        onAccepted: {
            visible = false
            field.commit(field.valueFromText(text))
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
        Keys.onEscapePressed: visible = false
    }

    component Arrow: Text {
        required property int direction
        y: (field.height - height) / 2
        width: Theme.space4
        visible: (hover.hovered || field.editing) && !field.held
        text: direction < 0 ? "‹" : "›"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
        horizontalAlignment: Text.AlignHCenter
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            onClicked: field.stepBy(parent.direction)
        }
    }

    Arrow {
        objectName: field.objectName + ":down"
        direction: -1
        x: 0
    }
    Arrow {
        objectName: field.objectName + ":up"
        direction: 1
        x: field.width - width
    }
}
