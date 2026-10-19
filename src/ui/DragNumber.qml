import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The one number field every view uses, after Blender's: drag sideways to change the value, Shift drags finely and
// Ctrl snaps to coarse steps, the hover arrows step it, and a click opens it for typing. Enter commits, Escape cancels.
// A gesture emits committed once, on release, so the action it runs is one undo step.
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
    // The action param the value goes to, which LinkedFields reads.
    property string param
    // A value another field is dragging to, such as a linked field; NaN shows value.
    property real previewValue: NaN
    property bool framed: true
    property alias font: label.font
    property color textColor: Theme.ink
    property var textFromValue: number => number.toFixed(decimals) + (unit ? " " + unit : "")
    // Returns NaN for text it cannot read. A leading + or - steps from the current value, and a/b divides.
    property var valueFromText: text => readNumber(text)

    readonly property bool dragging: drag.dragging
    readonly property bool editing: editor.visible
    // The value the field shows: the dragged value during a drag, else value.
    readonly property real margin: framed ? Theme.space4 : 0
    readonly property real shown: drag.dragging ? drag.preview : isNaN(previewValue) ? value : previewValue
    readonly property string displayText: textFromValue(shown) + (badge ? "   " + badge : "")

    signal committed(real value)
    // A drag in progress, so a linked field or view can preview it.
    signal previewed(real value)

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

    function commit(number) {
        if (!isNaN(number)) {
            field.committed(clamped(number))
        }
    }

    function startEditing() {
        editor.text = textFromValue(value)
        editor.visible = true
        editor.forceActiveFocus()
        editor.selectAll()
    }

    ToolTip.text: actionId ? Registry.actionInfo(actionId).title : ""
    ToolTip.visible: actionId !== "" && hover.hovered && !drag.dragging && !editing
    ToolTip.delay: Theme.tooltipDelay

    HoverHandler {
        id: hover
        cursorShape: editing ? Qt.IBeamCursor : Qt.SizeHorCursor
    }

    Rectangle {
        anchors.fill: parent
        visible: field.framed
        radius: Theme.radiusClip
        color: drag.dragging || hover.hovered ? Theme.rule : Theme.lift
    }

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: field.margin
        anchors.rightMargin: field.margin
        visible: !field.editing
        text: field.displayText
        color: field.enabled ? field.textColor : Theme.inkDim
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
        property real pressX
        property real startValue
        property real preview
        property bool dragging: false

        onPressed: mouse => {
            pressX = mouse.x
            startValue = field.value
            preview = field.value
            dragging = false
        }
        onPositionChanged: mouse => {
            const dx = mouse.x - pressX
            dragging = dragging || Math.abs(dx) >= Theme.dragThreshold
            if (!dragging) {
                return
            }
            const fine = mouse.modifiers & Qt.ShiftModifier
            const steps = dx / Theme.dragPixelsPerStep * (fine ? Theme.dragFineFactor : 1)
            const coarse = field.step * Theme.dragCoarseSteps
            const raw = startValue + steps * field.step
            preview = field.clamped(mouse.modifiers & Qt.ControlModifier ? Math.round(raw / coarse) * coarse : raw)
            field.previewed(preview)
        }
        onReleased: {
            if (dragging) {
                dragging = false
                if (preview !== startValue) {
                    field.committed(preview)
                }
            } else {
                field.startEditing()
            }
        }
        onCanceled: dragging = false
    }

    component Arrow: Text {
        required property int direction
        y: (field.height - height) / 2
        width: Theme.space4
        visible: hover.hovered && !field.editing && !drag.dragging
        text: direction < 0 ? "‹" : "›"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
        horizontalAlignment: Text.AlignHCenter
        MouseArea {
            anchors.fill: parent
            onClicked: field.commit(field.value + parent.direction * field.step)
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

    TextInput {
        id: editor
        objectName: field.objectName + ":text"
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
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
}
