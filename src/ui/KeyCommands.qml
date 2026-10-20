import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// Every action with its shortcut. Clicking a shortcut records the next key press as its new shortcut.
ColumnLayout {
    id: keys
    spacing: Theme.space3

    readonly property var commands: {
        const terms = search.text.toLowerCase().split(" ").filter(term => term !== "")
        return Registry.keyCommands.filter(command => terms.every(term =>
            (command.title + " " + command.category + " " + command.shortcut).toLowerCase().includes(term)))
    }

    TextField {
        id: search
        objectName: "keys.search"
        placeholderText: "Search actions and keys"
        ToolTip.text: placeholderText
        ToolTip.visible: hovered && !activeFocus
        ToolTip.delay: Theme.tooltipDelay
        placeholderTextColor: Theme.inkDim
        color: Theme.ink
        font.pixelSize: Theme.textBody
        Layout.preferredWidth: Theme.paletteWidth
        background: Rectangle {
            color: Theme.lift
            radius: Theme.radiusClip
        }
    }

    ListView {
        id: list
        objectName: "keys.list"
        clip: true
        model: keys.commands
        Layout.fillWidth: true
        Layout.fillHeight: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: RowLayout {
            id: row
            required property var modelData
            width: ListView.view.width
            height: Theme.buttonHeight
            spacing: Theme.space4
            Label {
                text: row.modelData.title
                color: Theme.ink
                font.pixelSize: Theme.textLabel
                elide: Text.ElideRight
                Layout.preferredWidth: Theme.paletteWidth / 2
            }
            Label {
                text: row.modelData.category
                color: Theme.inkDim
                font.pixelSize: Theme.textSmall
                elide: Text.ElideRight
                Layout.preferredWidth: Theme.labelWidth
            }
            // A focused text input turns the window's shortcuts off, so the key press reaches the recorder.
            Rectangle {
                radius: Theme.radiusClip
                color: recorder.activeFocus || hover.hovered ? Theme.lift : Theme.clear
                Layout.preferredWidth: Theme.labelWidth + Theme.space5
                Layout.fillHeight: true
                HoverHandler {
                    id: hover
                }
                TapHandler {
                    onTapped: recorder.forceActiveFocus()
                }
                TextInput {
                    id: recorder
                    objectName: "keys.setShortcut:" + row.modelData.id
                    anchors.fill: parent
                    leftPadding: Theme.space2
                    readOnly: true
                    text: activeFocus ? "Press keys, Escape to clear"
                                      : row.modelData.shortcut ? [row.modelData.shortcut].concat(row.modelData.extraShortcuts).map(Theme.shortcutText).join(", ")
                                                               : "Click to set"
                    color: activeFocus ? Theme.tape : row.modelData.shortcut ? Theme.ink : Theme.inkDim
                    font.pixelSize: Theme.textLabel
                    verticalAlignment: Text.AlignVCenter
                    Keys.onPressed: event => {
                        event.accepted = true
                        const clear = event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                        const sequence = clear ? "" : Registry.keySequence(event.key, event.modifiers)
                        if (clear || sequence !== "") {
                            focus = false
                            Registry.runAction("keys.setShortcut", {id: row.modelData.id, shortcut: sequence})
                        }
                    }
                }
            }
        }
    }
}
