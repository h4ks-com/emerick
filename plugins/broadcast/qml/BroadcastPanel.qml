import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App
import Emerick.Ui
import Emerick.Broadcast

// The broadcasts to Icecast servers: one row per profile with its state, and a form that adds or edits one.
Popup {
    id: panel
    property var editing: null
    property string format: "opus"
    property int bitrate: 128
    property bool tls: false
    anchors.centerIn: parent
    width: Math.min(Theme.paletteWidth, parent.width - 2 * Theme.space4)
    padding: Theme.space4
    modal: true
    focus: true
    visible: Broadcasts.panelOpen
    onClosed: {
        Broadcasts.panelOpen = false
        editing = null
    }
    background: GlassPanel {}

    function edit(profile) {
        editing = profile
        nameField.text = profile.name || ""
        hostField.text = profile.host || ""
        portField.text = String(profile.port || 8000)
        mountField.text = profile.mount || "/stream"
        userField.text = profile.user || "source"
        passwordField.text = ""
        streamNameField.text = profile.streamName || ""
        genreField.text = profile.genre || ""
        descriptionField.text = profile.description || ""
        format = profile.format || "opus"
        bitrate = profile.bitrate || 128
        tls = profile.tls || false
    }

    function save() {
        const saved = Registry.runAction("broadcast.save", {
            name: nameField.text, host: hostField.text, port: parseInt(portField.text), tls: tls,
            mount: mountField.text, user: userField.text, password: passwordField.text, format: format,
            bitrate: bitrate, streamName: streamNameField.text, genre: genreField.text,
            description: descriptionField.text
        })
        if (saved !== undefined && saved !== null)
            editing = null
    }

    function stateColor(state) {
        return state === "live" ? Theme.rec : state === "idle" ? Theme.inkDim : Theme.tape
    }

    component Field: TextField {
        property string hint
        Layout.fillWidth: true
        implicitHeight: Theme.buttonHeight
        leftPadding: Theme.space2
        rightPadding: Theme.space2
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.ink
        placeholderTextColor: Theme.inkDim
        font.pixelSize: Theme.textLabel
        ToolTip.text: hint
        ToolTip.visible: hovered && !activeFocus
        ToolTip.delay: Theme.tooltipDelay
        background: Rectangle {
            radius: Theme.radiusClip
            color: Theme.lift
            border.width: parent.activeFocus ? Theme.hairline : 0
            border.color: Theme.inkDim
        }
    }

    component Choice: ChromeButton {
        required property var value
        required property var current
        property var pick
        actionId: ""
        ToolTip.text: hint
        active: value === current
        run: () => pick(value)
    }

    contentItem: ColumnLayout {
        objectName: "broadcast.list"
        spacing: Theme.space2
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "Broadcasts"
                color: Theme.ink
                font.pixelSize: Theme.textTitle
            }
            ChromeButton {
                objectName: "broadcast.add"
                actionId: ""
                ToolTip.text: hint
                iconName: "plus"
                hint: "Add broadcast"
                visible: panel.editing === null
                run: () => panel.edit({})
            }
        }
        Label {
            visible: list.count === 0 && panel.editing === null
            text: "No broadcasts yet, click + to add one"
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.paletteListHeight)
            visible: panel.editing === null
            clip: true
            spacing: Theme.space1
            model: Broadcasts.list
            delegate: RowLayout {
                id: row
                required property var modelData
                readonly property bool running: modelData.state !== "idle"
                property bool confirming: false
                width: ListView.view.width
                spacing: Theme.space2
                Rectangle {
                    objectName: "broadcast.state:" + row.modelData.name
                    implicitWidth: Theme.space2
                    implicitHeight: Theme.space2
                    radius: width / 2
                    color: panel.stateColor(row.modelData.state)
                    HoverHandler {
                        id: dotHover
                    }
                    ToolTip.text: row.modelData.state
                    ToolTip.visible: dotHover.hovered
                    ToolTip.delay: Theme.tooltipDelay
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name + "   " + (row.modelData.state === "live"
                              ? new Date(row.modelData.uptimeSeconds * 1000).toISOString().substr(11, 8)
                              : row.modelData.state)
                        color: Theme.ink
                        font.pixelSize: Theme.textBody
                        font.features: {"tnum": 1}
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.lastError !== "" ? row.modelData.lastError
                              : row.modelData.host + ":" + row.modelData.port + row.modelData.mount + " · "
                                + row.modelData.format + " " + row.modelData.bitrate + " kbps"
                        color: row.modelData.lastError !== "" ? Theme.rec : Theme.inkDim
                        font.pixelSize: Theme.textSmall
                        elide: Text.ElideRight
                    }
                }
                ChromeButton {
                    objectName: (row.running ? "broadcast.stop:" : "broadcast.start:") + row.modelData.name
                    actionId: row.running ? "broadcast.stop" : "broadcast.start"
                    params: ({name: row.modelData.name})
                    iconName: row.running ? "stop" : "play"
                    active: row.running
                    activeColor: Theme.rec
                }
                ChromeButton {
                    objectName: "broadcast.edit:" + row.modelData.name
                    actionId: ""
                    ToolTip.text: hint
                    iconName: "pencil-simple"
                    hint: "Edit broadcast"
                    enabled: !row.running
                    run: () => panel.edit(row.modelData)
                }
                ChromeButton {
                    objectName: "broadcast.remove:" + row.modelData.name
                    actionId: "broadcast.remove"
                    iconName: "x"
                    active: row.confirming
                    activeColor: Theme.rec
                    hint: row.confirming ? "Click again to remove" : ""
                    run: () => {
                        if (row.confirming)
                            Registry.runAction("broadcast.remove", {name: row.modelData.name})
                        row.confirming = !row.confirming
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            visible: panel.editing !== null
            columns: 2
            columnSpacing: Theme.space2
            rowSpacing: Theme.space1
            Field {
                id: nameField
                objectName: "broadcast.save.name"
                placeholderText: "Name"
                hint: "Name the broadcast"
                readOnly: panel.editing !== null && panel.editing.name !== undefined
            }
            Field {
                id: streamNameField
                objectName: "broadcast.save.streamName"
                placeholderText: "Stream name"
                hint: "Name the stream for listeners"
            }
            Field {
                id: hostField
                objectName: "broadcast.save.host"
                placeholderText: "Server host"
                hint: "Type the Icecast server's host name"
            }
            Field {
                id: portField
                objectName: "broadcast.save.port"
                placeholderText: "Port"
                hint: "Type the server's port, 8000 by default"
                validator: IntValidator { bottom: 1; top: 65535 }
            }
            Field {
                id: mountField
                objectName: "broadcast.save.mount"
                placeholderText: "Mount"
                hint: "Type the mount to stream to, such as /live"
            }
            Field {
                id: userField
                objectName: "broadcast.save.user"
                placeholderText: "User"
                hint: "Type the source user, source by default"
            }
            Field {
                id: passwordField
                objectName: "broadcast.save.password"
                Layout.columnSpan: 2
                echoMode: TextInput.Password
                placeholderText: panel.editing && panel.editing.password ? "Leave empty to keep the password" : "Password"
                hint: "Type the server's source password"
            }
            Field {
                id: genreField
                objectName: "broadcast.save.genre"
                placeholderText: "Genre"
                hint: "Name the genre listeners see"
            }
            Field {
                id: descriptionField
                objectName: "broadcast.save.description"
                placeholderText: "Description"
                hint: "Describe the stream for listeners"
            }
            RowLayout {
                Layout.columnSpan: 2
                spacing: Theme.space1
                Choice {
                    objectName: "broadcast.save.format.opus"
                    text: "Opus"
                    hint: "Stream Opus in Ogg"
                    value: "opus"
                    current: panel.format
                    pick: value => panel.format = value
                }
                Choice {
                    objectName: "broadcast.save.format.mp3"
                    text: "MP3"
                    hint: "Stream MP3"
                    value: "mp3"
                    current: panel.format
                    pick: value => panel.format = value
                }
                Item {
                    implicitWidth: Theme.space3
                }
                Repeater {
                    model: [64, 96, 128, 192, 256, 320]
                    Choice {
                        required property int modelData
                        objectName: "broadcast.save.bitrate." + modelData
                        text: String(modelData)
                        hint: "Stream at " + modelData + " kbps"
                        value: modelData
                        current: panel.bitrate
                        pick: value => panel.bitrate = value
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ChromeButton {
                    objectName: "broadcast.save.tls"
                    actionId: ""
                    ToolTip.text: hint
                    text: "TLS"
                    hint: "Connect over TLS (https)"
                    active: panel.tls
                    run: () => panel.tls = !panel.tls
                }
            }
            RowLayout {
                Layout.columnSpan: 2
                Item {
                    Layout.fillWidth: true
                }
                ChromeButton {
                    objectName: "broadcast.cancelEdit"
                    actionId: ""
                    ToolTip.text: hint
                    text: "Cancel"
                    hint: "Close the form without saving"
                    run: () => panel.editing = null
                }
                ChromeButton {
                    objectName: "broadcast.save"
                    run: () => panel.save()
                }
            }
        }
    }
}
