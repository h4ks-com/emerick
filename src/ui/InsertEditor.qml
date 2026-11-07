import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The editor of one insert, floating over the window. Drag its title to move it, click it to raise it; Escape closes it.
Popup {
    id: editor
    required property string insertId
    readonly property var insert: Tracks.revision, Tracks.insert(insertId)

    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height - height - Theme.space5 * 2)
    z: Nav.editingInserts.indexOf(insertId)
    margins: Theme.space2
    padding: Theme.space3
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    Component.onCompleted: open()
    onClosed: if (Nav.editingInserts.includes(insertId)) Registry.runAction("insert.closeEditor", {insert: insertId})
    background: GlassPanel { color: Theme.slate }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content
            objectName: "insertEditor:" + editor.insertId
            anchors.fill: parent
            spacing: Theme.space3

            RowLayout {
                spacing: Theme.space2
                Layout.fillWidth: true

                RowLayout {
                    objectName: "insertEditor.titleBar:" + editor.insertId
                    spacing: Theme.space2
                    Layout.fillWidth: true

                    Grid {
                        columns: 2
                        spacing: Theme.hairline * 2
                        Repeater {
                            model: 6
                            Rectangle {
                                width: Theme.hairline * 2
                                height: width
                                radius: width / 2
                                color: titleHover.hovered || titleDrag.active ? Theme.ink : Theme.inkDim
                            }
                        }
                    }
                    Label {
                        text: editor.insert.title ?? ""
                        color: Theme.ink
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.Medium
                    }
                    Label {
                        text: editor.insert.trackName ?? ""
                        color: Theme.inkDim
                        font.pixelSize: Theme.textLabel
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    HoverHandler {
                        id: titleHover
                        cursorShape: titleDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    }
                    DragHandler {
                        id: titleDrag
                        target: null
                        property point start
                        onActiveChanged: if (active) start = Qt.point(editor.x, editor.y)
                        onTranslationChanged: {
                            editor.x = start.x + translation.x
                            editor.y = start.y + translation.y
                        }
                    }
                    ToolTip.text: "Drag to move"
                    ToolTip.visible: titleHover.hovered && !titleDrag.active
                    ToolTip.delay: Theme.tooltipDelay
                }
                ChromeButton {
                    objectName: "insert.openPluginWindow:" + (editor.insert.id ?? "")
                    visible: editor.insert.plugin !== undefined
                    actionId: "insert.openPluginWindow"
                    params: ({insert: editor.insert.id})
                    iconName: "sliders"
                    iconColor: Theme.inkDim
                    implicitWidth: Theme.headerButton
                    implicitHeight: Theme.headerButton
                }
                ChromeButton {
                    objectName: "insert.setBypass:" + (editor.insert.id ?? "") + ":editor"
                    actionId: "insert.setBypass"
                    params: ({insert: editor.insert.id, value: !editor.insert.bypass})
                    text: editor.insert.bypass ? "Turn on" : "Bypass"
                    active: editor.insert.bypass === true
                    implicitHeight: Theme.headerButton
                }
                ChromeButton {
                    objectName: "insert.closeEditor:" + editor.insertId
                    actionId: "insert.closeEditor"
                    params: ({insert: editor.insertId})
                    iconName: "x"
                    iconColor: Theme.inkDim
                    implicitWidth: Theme.headerButton
                    implicitHeight: Theme.headerButton
                }
            }

            Loader {
                active: editor.insert.id !== undefined
                sourceComponent: editor.insert.type === "eq" ? eq : panel
                Component {
                    id: eq
                    EqEditor {
                        insert: editor.insert
                    }
                }
                Component {
                    id: panel
                    Column {
                        spacing: Theme.space3
                        EffectPanel {
                            id: effect
                            insert: editor.insert
                        }
                        Row {
                            readonly property real db: Meters.reductions[editor.insert.id] ?? 0
                            visible: Effects.reportsReduction(editor.insert.type)
                            spacing: Theme.space2
                            Text {
                                text: "Gain reduction"
                                color: Theme.inkDim
                                font.pixelSize: Theme.textSmall
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Rectangle {
                                width: effect.width - Theme.labelWidth
                                height: Theme.meterBar * 3
                                radius: height / 2
                                color: Theme.graphite
                                anchors.verticalCenter: parent.verticalCenter
                                Rectangle {
                                    width: parent.width * Math.min(1, -parent.parent.db / Theme.reductionRangeDb)
                                    height: parent.height
                                    radius: parent.radius
                                    color: Theme.meterHot
                                }
                            }
                            Text {
                                text: parent.db.toFixed(1) + " dB"
                                color: Theme.ink
                                font.pixelSize: Theme.textSmall
                                font.features: {"tnum": 1}
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }
        }

        Item {
            anchors.fill: parent
            PointHandler {
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onActiveChanged: if (active) {
                    Nav.openEditor(editor.insertId)
                    editor.forceActiveFocus()
                }
            }
        }
    }
}
