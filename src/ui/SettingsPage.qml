import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml.Models
import Emerick.App

Rectangle {
    objectName: "settings"
    color: Theme.graphite

    readonly property var sections: Registry.sections("settings")

    component SettingRow: RowLayout {
        required property var modelData
        default property alias control: holder.data
        spacing: Theme.space4
        Label {
            text: parent.modelData.title
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
            Layout.preferredWidth: Theme.labelWidth
        }
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
        Label {
            text: parent.modelData.hint
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
        }
    }

    DelegateChooser {
        id: settingRow
        role: "type"
        DelegateChoice {
            roleValue: App.Color
            SettingRow {
                id: colorRow
                Rectangle {
                    objectName: colorRow.modelData.id
                    width: Theme.swatchSize
                    height: Theme.swatchSize
                    radius: Theme.radiusClip
                    color: Registry.settings[colorRow.modelData.id]
                }
            }
        }
        DelegateChoice {
            roleValue: App.Integer
            SettingRow {
                id: integerRow
                ThemeSpinBox {
                    objectName: integerRow.modelData.id
                    enabled: !integerRow.modelData.readOnly
                    from: integerRow.modelData.minimum
                    to: integerRow.modelData.maximum
                    editable: true
                    value: Registry.settings[objectName]
                    onValueModified: Registry.setSettingValue(objectName, value)
                }
            }
        }
        DelegateChoice {
            roleValue: App.Number
            SettingRow {
                id: numberRow
                TextField {
                    objectName: numberRow.modelData.id
                    enabled: !numberRow.modelData.readOnly
                    text: Registry.settings[objectName]
                    validator: DoubleValidator {}
                    onEditingFinished: Registry.setSettingValue(objectName, Number(text))
                }
            }
        }
        DelegateChoice {
            roleValue: App.String
            SettingRow {
                id: stringRow
                TextField {
                    objectName: stringRow.modelData.id
                    enabled: !stringRow.modelData.readOnly
                    text: Registry.settings[objectName]
                    onEditingFinished: Registry.setSettingValue(objectName, text)
                }
            }
        }
        DelegateChoice {
            roleValue: App.Boolean
            SettingRow {
                id: booleanRow
                Switch {
                    objectName: booleanRow.modelData.id
                    enabled: !booleanRow.modelData.readOnly
                    checked: Registry.settings[objectName]
                    onToggled: Registry.setSettingValue(objectName, checked)
                }
            }
        }
        DelegateChoice {
            roleValue: App.Choice
            SettingRow {
                id: choiceRow
                ThemeComboBox {
                    objectName: choiceRow.modelData.id
                    enabled: !choiceRow.modelData.readOnly
                    model: choiceRow.modelData.choices
                    currentIndex: model.indexOf(Registry.settings[objectName])
                    onActivated: index => Registry.setSettingValue(objectName, model[index])
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            color: Theme.slate
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space1
                Repeater {
                    model: sections
                    delegate: Button {
                        id: sectionButton
                        required property var modelData
                        readonly property bool current: modelData.key === Nav.settingsTab
                        objectName: "settings.show." + modelData.key
                        text: modelData.title
                        flat: true
                        Layout.fillWidth: true
                        onClicked: Registry.activate(objectName)
                        contentItem: Text {
                            text: sectionButton.text
                            color: sectionButton.current ? Theme.ink : Theme.inkDim
                            font.pixelSize: Theme.textBody
                        }
                        background: Rectangle {
                            color: sectionButton.current ? Theme.lift : Theme.clear
                            radius: Theme.radiusClip
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                ChromeButton {
                    objectName: "settings.close"
                    Layout.fillWidth: true
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.space5
            currentIndex: sections.findIndex(section => section.key === Nav.settingsTab)
            Repeater {
                model: sections
                delegate: ColumnLayout {
                    required property var modelData
                    objectName: modelData.id
                    spacing: Theme.space3
                    Repeater {
                        model: Registry.settingsAt(parent.modelData.id)
                        delegate: settingRow
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
