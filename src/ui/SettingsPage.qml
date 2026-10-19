import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml.Models
import Emerick.App

Rectangle {
    id: page
    objectName: "settings"
    color: Theme.graphite

    readonly property var sections: Registry.sections("settings")
    readonly property var current: sections.find(section => section.key === Nav.settingsTab)

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
            implicitWidth: Math.max(childrenRect.width, Theme.menuWidth)
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
                DragNumber {
                    objectName: integerRow.modelData.id
                    enabled: !integerRow.modelData.readOnly
                    from: integerRow.modelData.minimum
                    to: integerRow.modelData.maximum
                    value: Registry.settings[objectName]
                    onCommitted: number => Registry.setSettingValue(objectName, number)
                }
            }
        }
        DelegateChoice {
            roleValue: App.Number
            SettingRow {
                id: numberRow
                DragNumber {
                    objectName: numberRow.modelData.id
                    enabled: !numberRow.modelData.readOnly
                    from: numberRow.modelData.minimum
                    to: numberRow.modelData.maximum
                    step: Theme.numberSettingStep
                    decimals: Theme.numberSettingDecimals
                    value: Registry.settings[objectName]
                    onCommitted: number => Registry.setSettingValue(objectName, number)
                }
            }
        }
        DelegateChoice {
            roleValue: App.String
            SettingRow {
                id: stringRow
                Loader {
                    sourceComponent: stringRow.modelData.suggestions.length > 0 ? suggestedText : plainText
                }
                Component {
                    id: plainText
                    TextField {
                        objectName: stringRow.modelData.id
                        enabled: !stringRow.modelData.readOnly
                        text: Registry.settings[objectName]
                        onEditingFinished: Registry.setSettingValue(objectName, text)
                    }
                }
                Component {
                    id: suggestedText
                    SearchCombo {
                        objectName: stringRow.modelData.id
                        implicitWidth: Theme.menuWidth
                        enabled: !stringRow.modelData.readOnly
                        acceptsText: true
                        model: stringRow.modelData.suggestions
                        currentId: Registry.settings[objectName]
                        onPicked: entry => Registry.setSettingValue(objectName, entry.id)
                        onTyped: text => Registry.setSettingValue(objectName, text)
                    }
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
                SearchCombo {
                    objectName: choiceRow.modelData.id
                    enabled: !choiceRow.modelData.readOnly
                    model: choiceRow.modelData.choices.map(choice => ({id: String(choice), name: String(choice), value: choice}))
                    currentId: String(Registry.settings[objectName])
                    onPicked: entry => Registry.setSettingValue(objectName, entry.value)
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
                    model: page.sections
                    delegate: ColumnLayout {
                        id: sectionEntry
                        required property var modelData
                        required property int index
                        readonly property bool current: modelData.key === Nav.settingsTab
                        spacing: Theme.space1
                        Layout.fillWidth: true
                        Label {
                            objectName: "settings.group." + sectionEntry.modelData.group
                            visible: sectionEntry.modelData.group !== ""
                                     && (sectionEntry.index === 0
                                         || page.sections[sectionEntry.index - 1].group !== sectionEntry.modelData.group)
                            text: sectionEntry.modelData.group
                            color: Theme.inkDim
                            font.pixelSize: Theme.textSmall
                            font.weight: Font.Medium
                            Layout.topMargin: Theme.space4
                            Layout.leftMargin: Theme.space2
                        }
                        Button {
                            id: sectionButton
                            objectName: "settings.show." + sectionEntry.modelData.key
                            text: sectionEntry.modelData.title
                            flat: true
                            Layout.fillWidth: true
                            onClicked: Registry.activate(objectName)
                            contentItem: Text {
                                text: sectionButton.text
                                color: sectionEntry.current ? Theme.ink : Theme.inkDim
                                font.pixelSize: Theme.textBody
                            }
                            background: Rectangle {
                                color: sectionEntry.current ? Theme.lift : Theme.clear
                                radius: Theme.radiusClip
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.preferredWidth: Theme.hairline
            Layout.fillHeight: true
            color: Theme.rule
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                objectName: "settings.titleBar"
                color: Theme.slate
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.headerHeight
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space5
                    anchors.rightMargin: Theme.space3
                    spacing: Theme.space2
                    Label {
                        text: "Settings"
                        color: Theme.inkDim
                        font.pixelSize: Theme.textTitle
                    }
                    Label {
                        text: "›"
                        color: Theme.inkDim
                        font.pixelSize: Theme.textTitle
                    }
                    Label {
                        objectName: "settings.title"
                        text: page.current !== undefined ? page.current.title : ""
                        color: Theme.ink
                        font.pixelSize: Theme.textTitle
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }
                    ChromeButton {
                        objectName: "settings.close"
                        iconName: "x"
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.hairline
                    color: Theme.rule
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.space5
                currentIndex: page.sections.findIndex(section => section.key === Nav.settingsTab)
                Repeater {
                    model: page.sections
                    delegate: Loader {
                        id: sectionPage
                        required property var modelData
                        objectName: modelData.id
                        sourceComponent: modelData.key === "keys" ? keyCommands : settingsList
                    }
                }
            }
        }
    }

    Component {
        id: settingsList
        Flickable {
            id: scroller
            objectName: parent.objectName + ".list"
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: list
                width: scroller.width
                spacing: Theme.space3
                Repeater {
                    model: Registry.settingsAt(scroller.parent.objectName)
                    delegate: settingRow
                }
            }
        }
    }

    Component {
        id: keyCommands
        KeyCommands {}
    }

    Shortcut {
        sequence: "Escape"
        enabled: !(page.Window.activeFocusItem instanceof TextInput)
        onActivated: Registry.activate("settings.close")
    }
}
