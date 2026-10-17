import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Popup {
    id: commandPalette
    x: Math.round((parent.width - width) / 2)
    y: Theme.paletteTop
    width: Math.min(Theme.paletteWidth, parent.width - 2 * Theme.space4)
    padding: Theme.space2
    modal: true
    visible: Nav.paletteOpen
    onClosed: Nav.paletteOpen = false
    onOpened: {
        query.text = ""
        query.forceActiveFocus()
    }

    property var results: Registry.palette(query.text)

    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
    }

    function highlighted(title, ranges) {
        let html = ""
        let last = 0
        for (const range of ranges) {
            html += escaped(title.slice(last, range[0])) + "<b>" + escaped(title.slice(range[0], range[1])) + "</b>"
            last = range[1]
        }
        return html + escaped(title.slice(last))
    }

    function activate(index) {
        if (index < 0 || index >= results.length || !results[index].enabled) {
            return
        }
        const id = results[index].id
        Nav.paletteOpen = false
        Registry.activate(id)
    }

    background: GlassPanel {}

    contentItem: Column {
        objectName: "palette"
        spacing: Theme.space2

        TextField {
            id: query
            objectName: "palette.query"
            width: parent.width
            placeholderText: "Search actions and settings"
            placeholderTextColor: Theme.inkDim
            color: Theme.ink
            font.pixelSize: Theme.textTitle
            background: Rectangle {
                color: Theme.lift
                radius: Theme.radiusClip
            }
            onTextChanged: list.currentIndex = 0
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: commandPalette.activate(list.currentIndex)
            Keys.onEnterPressed: commandPalette.activate(list.currentIndex)
        }

        ListView {
            id: list
            objectName: "palette.results"
            width: parent.width
            height: Math.min(contentHeight, Theme.paletteListHeight)
            clip: true
            model: commandPalette.results
            highlightMoveDuration: 0
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: "palette.result." + modelData.id
                width: ListView.view.width
                height: Theme.listRowHeight
                enabled: modelData.enabled
                highlighted: ListView.isCurrentItem
                onClicked: commandPalette.activate(index)
                background: Rectangle {
                    color: row.highlighted || row.hovered ? Theme.lift : Theme.clear
                    radius: Theme.radiusClip
                }
                contentItem: Item {
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: commandPalette.highlighted(row.modelData.title, row.modelData.ranges)
                        textFormat: Text.StyledText
                        color: row.enabled ? Theme.ink : Theme.inkDim
                        font.pixelSize: Theme.textBody
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.shortcut ? Theme.shortcutText(row.modelData.shortcut) : row.modelData.category
                        color: Theme.inkDim
                        font.pixelSize: Theme.textLabel
                    }
                }
            }
        }

        Label {
            visible: commandPalette.results.length === 0
            text: "No match, try another word"
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
            leftPadding: Theme.space2
        }
    }
}
