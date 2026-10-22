import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A track's sends: per bus a level and a pre-fader switch, and in the full list an on switch and a remove button. The
// add button picks a bus to send to, or a new one.
ColumnLayout {
    id: list
    required property string trackId
    required property var sends
    property bool compact: false
    spacing: Theme.space1

    Repeater {
        model: list.sends
        RowLayout {
            id: row
            required property var modelData
            readonly property var target: ({track: list.trackId, bus: modelData.bus})
            spacing: Theme.space1
            Layout.fillWidth: true

            DragNumber {
                id: level
                objectName: "send.setLevel:" + list.trackId + ":" + row.modelData.bus
                actionId: "send.setLevel"
                value: row.modelData.db
                from: Theme.gainMinDb
                to: Theme.gainMaxDb
                step: Theme.gainStepDb
                decimals: 1
                textFromValue: number => row.modelData.busName + "  " + number.toFixed(1)
                valueFromText: text => level.readNumber(text.replace(row.modelData.busName, ""))
                textColor: row.modelData.enabled ? Theme.ink : Theme.inkDim
                font.pixelSize: Theme.textSmall
                implicitHeight: Theme.headerButton
                Layout.fillWidth: true
                actionsFor: number => [{id: "send.setLevel", params: Object.assign({db: number}, row.target)}]
            }
            ChromeButton {
                objectName: "send.setPre:" + list.trackId + ":" + row.modelData.bus
                actionId: "send.setPre"
                params: Object.assign({value: !row.modelData.pre}, row.target)
                active: row.modelData.pre
                text: "P"
                padding: 0
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
            }
            ChromeButton {
                objectName: "send.setEnabled:" + list.trackId + ":" + row.modelData.bus
                visible: !list.compact
                actionId: "send.setEnabled"
                params: Object.assign({value: !row.modelData.enabled}, row.target)
                active: row.modelData.enabled
                text: "On"
                implicitHeight: Theme.headerButton
            }
            ChromeButton {
                objectName: "send.remove:" + list.trackId + ":" + row.modelData.bus
                visible: !list.compact
                actionId: "send.remove"
                params: row.target
                iconName: "x"
                iconColor: Theme.inkDim
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
            }
        }
    }

    ChromeButton {
        objectName: "send.add:" + list.trackId
        actionId: "send.add"
        iconName: "plus"
        iconColor: Theme.inkDim
        menu: picker
        implicitHeight: Theme.headerButton
        Layout.fillWidth: list.compact
    }

    GlassMenu {
        id: picker
        objectName: "send.add:" + list.trackId + ":menu"
        property var buses: []
        onAboutToShow: buses = Registry.actionInfo("send.add").choices.filter(
                           bus => bus.id !== list.trackId && !list.sends.some(send => send.bus === bus.id))

        Instantiator {
            model: picker.buses
            delegate: ActionMenuItem {
                required property var modelData
                objectName: "send.add:" + list.trackId + ":" + modelData.id
                actionId: "send.add"
                title: modelData.title
                params: Object.assign({track: list.trackId}, modelData.params)
            }
            onObjectAdded: (index, object) => picker.insertItem(index, object)
            onObjectRemoved: (index, object) => picker.removeItem(object)
        }
        ActionMenuItem {
            objectName: "send.toNewBus:" + list.trackId
            actionId: "send.toNewBus"
            title: "Send to new bus"
            params: ({tracks: [list.trackId]})
        }
    }
}
