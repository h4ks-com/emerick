import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A track's insert slots in signal order, after Logic's: click a slot to open its editor, drag it to reorder it,
// right-click it to move or remove it, hover it for its remove and power buttons, click the gap above a slot to add an
// effect there, and click the empty slot to add one at the end.
ColumnLayout {
    id: list
    required property string trackId
    required property var inserts
    // Where the picker adds its effect; -1 adds it at the end.
    property int addIndex: -1
    spacing: Theme.space1

    function addParams(choice) {
        return Object.assign({track: list.trackId}, list.addIndex >= 0 ? {index: list.addIndex} : {}, choice)
    }

    Repeater {
        model: list.inserts
        Item {
            id: row
            required property var modelData
            required property int index
            property int dropIndex: index
            implicitHeight: Theme.headerButton
            Layout.fillWidth: true
            z: reorder.active ? 1 : 0
            transform: Translate { y: reorder.active ? reorder.translation.y : 0 }

            HoverHandler { id: hover }

            ChromeButton {
                id: slot
                readonly property var modelData: row.modelData
                readonly property int index: row.index
                objectName: "insert.edit:" + modelData.id
                actionId: "insert.edit"
                params: ({insert: modelData.id})
                text: modelData.title
                hint: !Registry.readOnly ? "Drag to reorder it, right-click to move or remove it"
                      : modelData.plugin ? modelData.plugin.format.toUpperCase() + " plugin" : "Built-in effect"
                active: Nav.editingInserts.includes(modelData.id)
                activeColor: Theme.inkDim
                opacity: modelData.bypass ? Theme.mutedOpacity * 1.5 : 1
                textSize: Theme.textSmall
                padding: Theme.space1
                implicitHeight: Theme.headerButton
                clip: true
                anchors.fill: parent

                // A MouseArea keeps the inspector's Flickable from stealing a vertical drag, which a DragHandler
                // cannot, so we run the click and the reorder drag here.
                MouseArea {
                    id: reorder
                    objectName: "insert.move:" + row.modelData.id + ":drag"
                    anchors.fill: parent
                    preventStealing: true
                    property real pressY
                    property real offset: 0
                    readonly property bool active: Math.abs(offset) >= Theme.dragThreshold
                    readonly property point translation: Qt.point(0, offset)
                    onPressed: mouse => {
                        slot.tipDismissed = true
                        pressY = mapToItem(null, mouse.x, mouse.y).y
                        offset = 0
                    }
                    onPositionChanged: mouse => {
                        if (Registry.readOnly || list.inserts.length < 2) {
                            return
                        }
                        offset = mapToItem(null, mouse.x, mouse.y).y - pressY
                        const steps = Math.round(offset / (row.height + list.spacing))
                        row.dropIndex = Math.max(0, Math.min(list.inserts.length - 1, row.index + steps))
                    }
                    onReleased: {
                        const moved = active && row.dropIndex !== row.index
                        const dragged = active
                        offset = 0
                        if (moved) {
                            Registry.runAction("insert.move", {insert: row.modelData.id, index: row.dropIndex})
                        } else if (!dragged) {
                            slot.clicked()
                        }
                    }
                    onCanceled: offset = 0
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: !Registry.readOnly
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
                        objectName: "insert.openPluginWindow:" + slot.modelData.id + ":menu"
                        actionId: "insert.openPluginWindow"
                        title: "Open plugin window"
                        available: slot.modelData.plugin !== undefined
                        params: ({insert: slot.modelData.id})
                    }
                    ActionMenuItem {
                        objectName: "insert.closePluginWindow:" + slot.modelData.id
                        actionId: "insert.closePluginWindow"
                        title: "Close plugin window"
                        available: slot.modelData.plugin !== undefined
                        params: ({insert: slot.modelData.id})
                    }
                    ActionMenuItem {
                        objectName: "insert.params:" + slot.modelData.id
                        actionId: "insert.params"
                        title: "List insert settings"
                        available: slot.modelData.plugin !== undefined
                        params: ({insert: slot.modelData.id})
                    }
                    ActionMenuItem {
                        objectName: "insert.remove:" + slot.modelData.id
                        actionId: "insert.remove"
                        title: "Remove insert"
                        params: ({insert: slot.modelData.id})
                    }
                }
            }

            ChromeButton {
                objectName: "insert.remove:" + row.modelData.id + ":slot"
                visible: !Registry.readOnly
                opacity: hover.hovered ? 1 : 0
                actionId: "insert.remove"
                params: ({insert: row.modelData.id})
                iconName: "x"
                iconColor: Theme.inkDim
                anchors.left: parent.left
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
            }

            ChromeButton {
                objectName: "insert.setBypass:" + row.modelData.id + ":slot"
                visible: !Registry.readOnly
                opacity: hover.hovered || row.modelData.bypass ? 1 : 0
                actionId: "insert.setBypass"
                params: ({insert: row.modelData.id, value: !row.modelData.bypass})
                hint: row.modelData.bypass ? "Turn it on" : "Turn it off"
                iconName: "power"
                iconColor: row.modelData.bypass ? Theme.inkDim : Theme.ink
                anchors.right: parent.right
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
            }

            // The gap reaches into the slots' edges so a thin spacing is still easy to hit.
            MouseArea {
                id: gap
                objectName: "insert.add:" + list.trackId + ":before:" + row.modelData.id
                visible: !Registry.readOnly
                hoverEnabled: true
                width: parent.width
                height: list.spacing + Theme.space1
                y: -(list.spacing + height) / 2
                onClicked: {
                    list.addIndex = row.index
                    picker.popup(gap, 0, gap.height)
                }
                ToolTip.text: "Add effect here"
                ToolTip.visible: containsMouse && !picker.opened
                ToolTip.delay: Theme.tooltipDelay

                Rectangle {
                    visible: gap.containsMouse
                    width: parent.width
                    height: Theme.selectionWidth
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.tape
                }
            }
        }
    }

    ChromeButton {
        objectName: "insert.add:" + list.trackId
        visible: !Registry.readOnly
        actionId: "insert.add"
        textSize: Theme.textSmall
        padding: Theme.space1
        menu: picker
        onPressed: list.addIndex = -1
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
                    params: list.addParams(modelData.params)
                }
                onObjectAdded: (index, object) => picker.insertItem(index, object)
                onObjectRemoved: (index, object) => picker.removeItem(object)
            }
            GlassMenu {
                id: plugins
                objectName: "insert.addPlugin:" + list.trackId
                title: "Plugins"
                enabled: pluginChoices.count > 0
                Instantiator {
                    id: pluginChoices
                    model: Registry.actionInfo("insert.addPlugin").choices ?? []
                    delegate: ActionMenuItem {
                        required property var modelData
                        objectName: "insert.addPlugin:" + list.trackId + ":" + modelData.id
                        actionId: "insert.addPlugin"
                        title: modelData.title
                        params: list.addParams(modelData.params)
                    }
                    onObjectAdded: (index, object) => plugins.insertItem(index, object)
                    onObjectRemoved: (index, object) => plugins.removeItem(object)
                }
            }
        }
    }
}
