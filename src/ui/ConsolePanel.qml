import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The Lua console: the action log and the REPL in one stream, above a Lua prompt with completion.
Rectangle {
    id: panel
    objectName: "console"
    color: Theme.graphite

    signal resizeBy(real delta)

    readonly property var filters: [
        {key: "all", title: "All", value: App.ConsoleFilter.All},
        {key: "info", title: "Info", value: App.ConsoleFilter.Info},
        {key: "output", title: "Output", value: App.ConsoleFilter.Output},
        {key: "errors", title: "Errors", value: App.ConsoleFilter.Errors}
    ]
    // The prompt takes these keys from the window, so they still work while it has focus.
    readonly property var promptActions: ["console.clear", "view.toggleConsole"]

    Component.onCompleted: input.forceActiveFocus()

    Rectangle {
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }

    MouseArea {
        objectName: "console.resize"
        y: -Theme.splitterGrip / 2
        width: parent.width
        height: Theme.splitterGrip
        z: 1
        cursorShape: Qt.SizeVerCursor
        property real pressY
        onPressed: mouse => pressY = mouse.y
        onPositionChanged: mouse => panel.resizeBy(pressY - mouse.y)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.hairline
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.buttonHeight + Theme.space2
            color: Theme.slate
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space3
                anchors.rightMargin: Theme.space2
                spacing: Theme.space1
                Label {
                    text: "Console"
                    color: Theme.inkDim
                    font.pixelSize: Theme.textLabel
                    rightPadding: Theme.space2
                }
                Repeater {
                    model: panel.filters
                    delegate: ChromeButton {
                        required property var modelData
                        objectName: "console.setFilter:" + modelData.key
                        actionId: "console.setFilter"
                        params: ({filter: modelData.key})
                        text: modelData.title
                        active: Console.filter === modelData.value
                        activeColor: Theme.inkDim
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ChromeButton {
                    objectName: "console.clear"
                    text: "Clear"
                }
                ChromeButton {
                    objectName: "view.toggleConsole"
                    iconName: "x"
                }
            }
        }

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: output.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // We keep the newest line in view until the user scrolls up.
            property bool following: true
            onContentHeightChanged: if (following) contentY = Math.max(0, contentHeight - height)
            onHeightChanged: if (following) contentY = Math.max(0, contentHeight - height)
            onContentYChanged: following = contentY + height >= contentHeight - Theme.space4

            TextEdit {
                id: output
                objectName: "console.output"
                width: scroller.width
                padding: Theme.space3
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                color: Theme.ink
                selectionColor: Theme.rule
                selectedTextColor: Theme.ink
                font.family: Theme.monoFamily
                font.pixelSize: Theme.textLabel
                Component.onCompleted: Console.attachView(textDocument, Object.assign({
                    input: Theme.inkDim,
                    print: Theme.ink,
                    result: Theme.ink,
                    error: Theme.rec,
                    action: Theme.ink,
                    source: Theme.inkDim
                }, Theme.codeColors))
                Component.onDestruction: Console.attachView(null, {})

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        const line = Console.entryAt(output.positionAt(eventPoint.position.x, eventPoint.position.y))
                        if (line.serial !== undefined) {
                            lineMenu.line = line
                            lineMenu.popup()
                        }
                    }
                }
            }

            Text {
                visible: output.length === 0
                x: Theme.space3
                y: Theme.space3
                text: "Type Lua below, such as help() or emerick.run(\"track.addMidi\"). Actions you run show here as Lua."
                color: Theme.inkDim
                font.family: Theme.monoFamily
                font.pixelSize: Theme.textLabel
            }
        }

        GlassMenu {
            id: lineMenu
            property var line: ({})
            ActionMenuItem {
                objectName: "console.copyEntry"
                actionId: "console.copyEntry"
                title: Registry.actionInfo(actionId).title
                params: ({entry: lineMenu.line.serial})
            }
            ActionMenuItem {
                objectName: "console.runAgain"
                actionId: "console.runAgain"
                title: Registry.actionInfo(actionId).title
                available: lineMenu.line.kind === App.ConsoleLine.Action
                params: ({entry: lineMenu.line.serial})
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.hairline
            color: Theme.rule
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            Layout.topMargin: Theme.space1
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space1
            Text {
                Layout.alignment: Qt.AlignTop
                topPadding: input.topPadding
                text: ">"
                color: Theme.tape
                font: input.font
            }
            TextArea {
                id: input
                objectName: "console.eval"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(implicitHeight, topPadding + bottomPadding + Theme.consoleInputLines * fontMetrics.lineSpacing)
                leftPadding: 0
                wrapMode: TextEdit.Wrap
                placeholderText: "Enter Lua to run, Shift+Enter adds a line, Tab completes"
                placeholderTextColor: Theme.inkDim
                color: Theme.ink
                selectionColor: Theme.rule
                selectedTextColor: Theme.ink
                font.family: Theme.monoFamily
                font.pixelSize: Theme.textLabel
                background: null

                property var completion: ({from: 0, to: 0, items: []})
                property bool dismissed: false
                // We read focus, which an agent driving an inactive window keeps, so completion shows for it too.
                readonly property bool completing: focus && !dismissed && completion.items.length > 0

                FontMetrics {
                    id: fontMetrics
                    font: input.font
                }
                CodeHighlighter {
                    document: input.textDocument
                    colors: Theme.codeColors
                }

                function refresh() {
                    completion = Console.completions(text, cursorPosition)
                    completions.currentIndex = 0
                }
                function accept(index) {
                    const item = completion.items[index]
                    const from = completion.from
                    remove(from, completion.to)
                    insert(from, item.insert)
                    cursorPosition = from + item.insert.length
                }
                function recall(step) {
                    text = Console.historyStep(step, text)
                    cursorPosition = text.length
                    dismissed = true
                }
                function run() {
                    const code = text
                    text = ""
                    Registry.runAction("console.eval", {code: code})
                }

                onTextChanged: {
                    dismissed = false
                    refresh()
                }
                onCursorPositionChanged: refresh()

                Keys.onPressed: event => {
                    const sequence = Registry.keySequence(event.key, event.modifiers)
                    const shortcut = panel.promptActions.find(id => Registry.actionInfo(id).shortcut === sequence)
                    if (shortcut !== undefined) {
                        event.accepted = true
                        Registry.activate(shortcut)
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                        event.accepted = true
                        run()
                    } else if (event.key === Qt.Key_Tab) {
                        event.accepted = true
                        if (completing) {
                            accept(completions.currentIndex)
                        } else {
                            insert(cursorPosition, "    ")
                        }
                    } else if (event.key === Qt.Key_Escape && completing) {
                        event.accepted = true
                        dismissed = true
                    } else if (event.key === Qt.Key_Up) {
                        if (completing) {
                            event.accepted = true
                            completions.decrementCurrentIndex()
                        } else if (text.lastIndexOf("\n", cursorPosition - 1) < 0) {
                            event.accepted = true
                            recall(-1)
                        }
                    } else if (event.key === Qt.Key_Down) {
                        if (completing) {
                            event.accepted = true
                            completions.incrementCurrentIndex()
                        } else if (text.indexOf("\n", cursorPosition) < 0) {
                            event.accepted = true
                            recall(1)
                        }
                    }
                }

                Popup {
                    objectName: "console.completions"
                    x: Math.min(input.cursorRectangle.x, input.width - width)
                    y: -height - Theme.space1
                    width: Math.min(Theme.completionWidth, panel.width - 2 * Theme.space3)
                    height: Math.min(input.completion.items.length, Theme.completionRows) * Theme.buttonHeight + topPadding + bottomPadding
                    padding: Theme.space1
                    visible: input.completing
                    focus: false
                    closePolicy: Popup.NoAutoClose
                    background: GlassPanel {}
                    contentItem: ListView {
                        id: completions
                        clip: true
                        model: input.completion.items
                        highlightMoveDuration: 0
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollIndicator.vertical: ScrollIndicator {}
                        delegate: ItemDelegate {
                            id: suggestion
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: Theme.buttonHeight
                            focusPolicy: Qt.NoFocus
                            highlighted: ListView.isCurrentItem
                            onClicked: input.accept(index)
                            background: Rectangle {
                                radius: Theme.radiusClip
                                color: suggestion.highlighted || suggestion.hovered ? Theme.lift : Theme.clear
                            }
                            contentItem: RowLayout {
                                spacing: Theme.space3
                                Text {
                                    text: Theme.highlighted(suggestion.modelData.label, suggestion.modelData.ranges)
                                    textFormat: Text.StyledText
                                    color: Theme.ink
                                    font.family: Theme.monoFamily
                                    font.pixelSize: Theme.textLabel
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: suggestion.modelData.detail
                                    color: Theme.inkDim
                                    font.pixelSize: Theme.textSmall
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
