import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A track's insert slots in signal order, after Logic's: click a slot to open its editor, right-click it to bypass,
// move or remove it, and click the empty slot to add an effect.
ColumnLayout {
    id: list
    required property string trackId
    required property var inserts
    spacing: Theme.space1

    Repeater {
        model: list.inserts
        ChromeButton {
            id: slot
            required property var modelData
            required property int index
            objectName: "insert.edit:" + modelData.id
            actionId: "insert.edit"
            params: ({insert: modelData.id})
            text: modelData.title
            hint: "Right-click to bypass, move or remove it"
            active: Nav.editingInsert === modelData.id
            activeColor: Theme.inkDim
            opacity: modelData.bypass ? Theme.mutedOpacity * 1.5 : 1
            textSize: Theme.textSmall
            padding: Theme.space1
            implicitHeight: Theme.headerButton
            Layout.fillWidth: true

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: actions.popup()
            }

            GlassMenu {
                id: actions
                objectName: "insert.edit:" + slot.modelData.id + ":menu"
                ActionMenuItem {
                    objectName: "insert.setBypass:" + slot.modelData.id
                    actionId: "insert.setBypass"
                    title: slot.modelData.bypass ? "Turn insert on" : "Bypass insert"
                    params: ({insert: slot.modelData.id, value: !slot.modelData.bypass})
                }
                ActionMenuItem {
                    objectName: "insert.move:" + slot.modelData.id + ":up"
                    actionId: "insert.move"
                    title: "Move insert up"
                    available: slot.index > 0
                    params: ({insert: slot.modelData.id, index: slot.index - 1})
                }
                ActionMenuItem {
                    objectName: "insert.move:" + slot.modelData.id + ":down"
                    actionId: "insert.move"
                    title: "Move insert down"
                    available: slot.index < list.inserts.length - 1
                    params: ({insert: slot.modelData.id, index: slot.index + 1})
                }
                ActionMenuItem {
                    objectName: "insert.remove:" + slot.modelData.id
                    actionId: "insert.remove"
                    title: "Remove insert"
                    params: ({insert: slot.modelData.id})
                }
            }
        }
    }

    ChromeButton {
        objectName: "insert.add:" + list.trackId
        actionId: "insert.add"
        iconName: "plus"
        iconColor: Theme.inkDim
        menu: picker
        implicitHeight: Theme.headerButton
        Layout.fillWidth: true

        GlassMenu {
            id: picker
            objectName: "insert.add:" + list.trackId + ":menu"
            Instantiator {
                model: Registry.actionInfo("insert.add").choices
                delegate: ActionMenuItem {
                    required property var modelData
                    objectName: "insert.add:" + list.trackId + ":" + modelData.id
                    actionId: "insert.add"
                    title: modelData.title
                    params: Object.assign({track: list.trackId}, modelData.params)
                }
                onObjectAdded: (index, object) => picker.insertItem(index, object)
                onObjectRemoved: (index, object) => picker.removeItem(object)
            }
        }
    }
}
