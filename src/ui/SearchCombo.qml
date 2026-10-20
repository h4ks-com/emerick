import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.App

// The one picker every view uses. Each entry has an id and a name. Typing filters the names, the arrows move,
// Enter picks and Escape closes. With acceptsText, Enter on no match hands over the typed text.
Item {
    id: control
    property var model: []
    property string currentId
    property bool acceptsText: false
    property string toolTip
    // What the closed picker shows when no entry has currentId.
    property string missingText: currentId
    readonly property var current: model.find(entry => entry.id === currentId)
    readonly property string displayText: current !== undefined ? current.name : missingText
    readonly property alias popup: popup
    readonly property alias field: field
    readonly property var matches: {
        const terms = popup.opened ? field.text.toLowerCase().split(" ").filter(term => term !== "") : []
        return model.filter(entry => terms.every(term => entry.name.toLowerCase().includes(term)))
    }

    signal picked(var entry)
    signal typed(string text)

    implicitWidth: Theme.labelWidth
    implicitHeight: Theme.buttonHeight

    function open() {
        if (popup.opened) {
            return
        }
        field.text = ""
        field.forceActiveFocus()
        popup.open()
        list.currentIndex = Math.max(0, matches.findIndex(entry => entry.id === control.currentId))
    }

    function close() {
        popup.close()
        field.focus = false
        field.text = Qt.binding(() => control.displayText)
        field.cursorPosition = 0
    }

    function accept() {
        const index = list.currentIndex
        if (index >= 0 && index < matches.length) {
            const entry = matches[index]
            close()
            control.picked(entry)
        } else if (acceptsText && field.text !== "") {
            const text = field.text
            close()
            control.typed(text)
        }
    }

    TextField {
        id: field
        objectName: control.objectName + ":search"
        anchors.fill: parent
        leftPadding: Theme.space2
        rightPadding: Theme.space2 + Theme.iconSize
        text: control.displayText
        placeholderText: control.displayText
        placeholderTextColor: Theme.inkDim
        color: control.enabled ? Theme.ink : Theme.inkDim
        selectionColor: Theme.rule
        selectedTextColor: Theme.ink
        font.pixelSize: Theme.textLabel
        background: Rectangle {
            radius: Theme.radiusClip
            color: popup.opened || field.hovered ? Theme.rule : Theme.lift
        }
        ToolTip.text: control.toolTip
        ToolTip.visible: control.toolTip !== "" && hovered && !popup.opened
        ToolTip.delay: Theme.tooltipDelay
        onPressed: control.open()
        onTextEdited: {
            if (!popup.opened) {
                popup.open()
            }
            list.currentIndex = 0
        }
        onActiveFocusChanged: if (!activeFocus) control.close()
        Keys.onDownPressed: popup.opened ? list.incrementCurrentIndex() : control.open()
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onReturnPressed: popup.opened ? control.accept() : control.open()
        Keys.onEnterPressed: popup.opened ? control.accept() : control.open()
        Keys.onEscapePressed: event => {
            event.accepted = popup.opened
            control.close()
        }
    }

    ColorImage {
        x: control.width - width - Theme.space2
        y: (control.height - height) / 2
        color: Theme.inkDim
        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Basic/images/double-arrow.png"
    }

    // The height follows the match count, never the list's own content height, so layout settles in one pass.
    Popup {
        id: popup
        y: control.height + Theme.space1
        width: Math.max(control.width, Theme.labelWidth)
        height: Math.min(Math.max(list.count, 1), Theme.pickerRows) * Theme.buttonHeight + topPadding + bottomPadding
        padding: Theme.space1
        closePolicy: Popup.CloseOnPressOutsideParent
        onClosed: control.close()
        background: GlassPanel {}
        contentItem: ListView {
            id: list
            objectName: control.objectName + ":list"
            clip: true
            model: control.matches
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                height: Theme.buttonHeight
                focusPolicy: Qt.NoFocus
                highlighted: ListView.isCurrentItem
                onClicked: {
                    list.currentIndex = index
                    control.accept()
                }
                contentItem: Text {
                    text: row.modelData.name
                    color: Theme.ink
                    font.pixelSize: Theme.textLabel
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusClip
                    color: row.highlighted || row.hovered ? Theme.lift : Theme.clear
                }
            }
            Text {
                anchors.fill: parent
                leftPadding: Theme.space2
                visible: list.count === 0
                text: control.acceptsText ? "Press Enter to use this text" : "Clear the search to see all"
                color: Theme.inkDim
                font.pixelSize: Theme.textLabel
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
