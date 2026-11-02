import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A track's sends: per bus its name over its level, a pre-fader switch, and in the full list an on switch and a remove
// button. The add button picks a bus to send to, or a new one.
ColumnLayout {
    id: list
    required property string trackId
    required property var sends
    property bool compact: false
    spacing: Theme.space1

    Repeater {
        model: list.sends
        ColumnLayout {
            id: send
            required property var modelData
            readonly property var target: ({track: list.trackId, bus: modelData.bus})
            spacing: Theme.hairline
            Layout.fillWidth: true

            RowLayout {
                spacing: Theme.space1
                Layout.fillWidth: true

                Label {
                    objectName: "send.bus:" + list.trackId + ":" + send.modelData.bus
                    text: send.modelData.busName
                    color: send.modelData.enabled ? Theme.ink : Theme.inkDim
                    font.pixelSize: Theme.textSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    ToolTip.text: "Send to " + send.modelData.busName
                    ToolTip.visible: name.hovered
                    ToolTip.delay: Theme.tooltipDelay
                    HoverHandler {
                        id: name
                    }
                }
                ChromeButton {
                    objectName: "send.setPre:" + list.trackId + ":" + send.modelData.bus
                    actionId: "send.setPre"
                    enabled: !Registry.readOnly
                    params: Object.assign({value: !send.modelData.pre}, send.target)
                    active: send.modelData.pre
                    text: "P"
                    padding: 0
                    implicitWidth: Theme.headerButton
                    implicitHeight: Theme.headerButton
                }
                ChromeButton {
                    objectName: "send.setEnabled:" + list.trackId + ":" + send.modelData.bus
                    visible: !list.compact && !Registry.readOnly
                    actionId: "send.setEnabled"
                    params: Object.assign({value: !send.modelData.enabled}, send.target)
                    active: send.modelData.enabled
                    text: "On"
                    implicitHeight: Theme.headerButton
                }
                ChromeButton {
                    objectName: "send.remove:" + list.trackId + ":" + send.modelData.bus
                    visible: !list.compact && !Registry.readOnly
                    actionId: "send.remove"
                    params: send.target
                    iconName: "x"
                    iconColor: Theme.inkDim
                    implicitWidth: Theme.headerButton
                    implicitHeight: Theme.headerButton
                }
            }

            DragNumber {
                objectName: "send.setLevel:" + list.trackId + ":" + send.modelData.bus
                actionId: "send.setLevel"
                enabled: !Registry.readOnly
                value: send.modelData.db
                from: Theme.gainMinDb
                to: Theme.gainMaxDb
                step: Theme.gainStepDb
                decimals: 1
                unit: list.compact ? "" : "dB"
                textColor: send.modelData.enabled ? Theme.ink : Theme.inkDim
                font.pixelSize: Theme.textSmall
                implicitWidth: Theme.gainReadoutWidth
                implicitHeight: Theme.headerButton
                Layout.fillWidth: true
                actionsFor: number => [{id: "send.setLevel", params: Object.assign({db: number}, send.target)}]
            }
        }
    }

    ChromeButton {
        objectName: "send.add:" + list.trackId
        visible: !Registry.readOnly
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
