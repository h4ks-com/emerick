import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The one number field every view uses, with step buttons inside a raised field.
SpinBox {
    id: control
    implicitHeight: Theme.buttonHeight
    font.pixelSize: Theme.textLabel
    font.features: {"tnum": 1}
    textFromValue: value => String(value)
    valueFromText: text => parseInt(text)

    component Step: Rectangle {
        required property bool pressed
        required property string glyph
        implicitWidth: Theme.buttonHeight
        implicitHeight: Theme.buttonHeight
        radius: Theme.radiusClip
        color: pressed ? Theme.rule : Theme.clear
        Text {
            anchors.centerIn: parent
            text: parent.glyph
            color: Theme.inkDim
            font.pixelSize: Theme.textBody
        }
    }

    contentItem: TextInput {
        text: control.displayText
        color: control.enabled ? Theme.ink : Theme.inkDim
        selectionColor: Theme.rule
        selectedTextColor: Theme.ink
        font: control.font
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: control.editable
    }
    up.indicator: Step {
        x: control.width - width
        pressed: control.up.pressed
        glyph: "+"
    }
    down.indicator: Step {
        pressed: control.down.pressed
        glyph: "−"
    }
    background: Rectangle {
        implicitWidth: 3 * Theme.buttonHeight
        radius: Theme.radiusClip
        color: Theme.lift
    }
}
