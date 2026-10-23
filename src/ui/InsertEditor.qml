import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The editor of the insert Nav names, floating over the window. Drag its title to move it; Escape closes it.
Popup {
    id: editor
    readonly property var insert: Tracks.revision, Nav.editingInsert !== "" ? Tracks.insert(Nav.editingInsert) : ({})
    readonly property bool shown: insert.id !== undefined

    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height - height - Theme.space5 * 2)
    padding: Theme.space3
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    onShownChanged: shown ? open() : close()
    onClosed: if (Nav.editingInsert !== "") Registry.runAction("insert.closeEditor", {})
    background: GlassPanel {}

    contentItem: ColumnLayout {
        objectName: "insertEditor"
        spacing: Theme.space3

        RowLayout {
            spacing: Theme.space2
            Layout.fillWidth: true

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

                DragHandler {
                    target: null
                    property point start
                    onActiveChanged: if (active) start = Qt.point(editor.x, editor.y)
                    onTranslationChanged: {
                        editor.x = start.x + translation.x
                        editor.y = start.y + translation.y
                    }
                }
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
                objectName: "insert.closeEditor"
                iconName: "x"
                iconColor: Theme.inkDim
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
            }
        }

        Loader {
            active: editor.shown
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
                        visible: editor.insert.type === "compressor"
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
}
