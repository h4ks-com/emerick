import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

Rectangle {
    id: header

    property int index
    property string trackId
    property string name
    property string icon
    // A plugin's track kind can show its own items beside the track name.
    property string headerComponent
    property color trackColor
    property bool mute
    property bool solo
    property bool armed
    property bool monitor
    property real gainDb
    property real pan
    // A bus has no input, so it hides its arm and monitor buttons but keeps their room to line up with the tracks.
    property bool records: true
    // The row a drag would move this track to, or -1 while no drag runs.
    property int dropIndex: -1
    property string automationMode
    property var automationParams: []
    property string automationParam
    readonly property bool showsAutomation: Nav.automationOpen && automationParams.length > 0
    readonly property var automationModes: Registry.actionInfo("automation.setMode").choices

    signal contextRequested()

    readonly property bool selected: Nav.selectedTracks.indexOf(trackId) >= 0
    readonly property bool roomy: height >= Theme.headerButton * 2 + Theme.space3
    // A clicked header takes Cmd+A to select every track, and a press elsewhere gives the key back to the window.
    property bool editKeys: true

    color: selected ? Theme.rule : Theme.slate

    Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_A && event.modifiers === Qt.ControlModifier
    Keys.onPressed: event => {
        if (event.key === Qt.Key_A && event.modifiers === Qt.ControlModifier) {
            event.accepted = true
            Registry.runAction("track.selectAll", {})
        }
    }

    HoverHandler {
        id: hover
    }

    MouseArea {
        id: grab
        objectName: "track.move:" + header.trackId
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property real pressY
        property bool menuPress: false
        onPressed: mouse => {
            menuPress = Theme.isContextClick(mouse)
            if (menuPress) {
                header.contextRequested()
                Registry.runAction("track.showMenu", {track: header.trackId})
                return
            }
            pressY = mapToItem(null, 0, mouse.y).y
        }
        onPositionChanged: mouse => {
            if (menuPress) {
                return
            }
            const dy = mapToItem(null, 0, mouse.y).y - pressY
            if (Math.abs(dy) < Theme.dragThreshold) {
                return
            }
            const target = Math.max(0, Math.min(Tracks.rowCount() - 1, Math.round(header.index + dy / header.height)))
            header.dropIndex = target > header.index ? target + 1 : target
        }
        onReleased: {
            if (header.dropIndex >= 0) {
                const target = header.dropIndex > header.index ? header.dropIndex - 1 : header.dropIndex
                header.dropIndex = -1
                if (target !== header.index) {
                    Registry.runAction("track.move", {track: header.trackId, index: target})
                }
            }
        }
        onClicked: mouse => {
            if (menuPress) {
                return
            }
            header.forceActiveFocus()
            Registry.runAction("track.select", {track: header.trackId, extend: (mouse.modifiers & Qt.ControlModifier) !== 0,
                                                range: (mouse.modifiers & Qt.ShiftModifier) !== 0})
        }
        onDoubleClicked: if (!menuPress) Registry.runAction("track.beginRename", {track: header.trackId})
    }

    Rectangle {
        width: Theme.colorStripe
        height: parent.height
        color: header.trackColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.colorStripe + Theme.space2
        anchors.rightMargin: Theme.space2 + meter.width
        anchors.topMargin: Theme.space1
        anchors.bottomMargin: Theme.space1
        spacing: Theme.space1

        RowLayout {
            spacing: Theme.space1
            Layout.fillWidth: true
            Layout.alignment: header.roomy ? Qt.AlignTop : Qt.AlignVCenter

            ChromeButton {
                objectName: "track.select:" + header.trackId
                actionId: "track.select"
                params: ({track: header.trackId})
                iconName: header.icon
                iconColor: header.selected ? Theme.ink : Theme.inkDim
                background: null
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space1
                Label {
                    anchors.fill: parent
                    visible: !rename.visible
                    text: header.name
                    color: Theme.ink
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                TextField {
                    id: rename
                    objectName: "track.rename:" + header.trackId
                    anchors.fill: parent
                    visible: Nav.renamingTrack === header.trackId
                    ToolTip.text: Registry.actionInfo("track.rename").title
                    ToolTip.visible: hovered && !activeFocus
                    ToolTip.delay: Theme.tooltipDelay
                    padding: 0
                    leftPadding: Theme.space1
                    color: Theme.ink
                    font.pixelSize: Theme.textBody
                    background: Rectangle {
                        color: Theme.graphite
                        radius: Theme.radiusClip / 2
                        border.color: Theme.rule
                    }
                    // A hidden field keeps the focus it had, which would turn the window's shortcuts off.
                    onVisibleChanged: {
                        if (visible) {
                            text = header.name
                            forceActiveFocus()
                            selectAll()
                        } else {
                            focus = false
                        }
                    }
                    onAccepted: {
                        if (text.length > 0 && text !== header.name) {
                            Registry.runAction("track.rename", {track: header.trackId, name: text})
                        }
                        Nav.renamingTrack = ""
                    }
                    onActiveFocusChanged: if (!activeFocus && visible) Nav.renamingTrack = ""
                    Keys.onEscapePressed: Nav.renamingTrack = ""
                }
            }

            Loader {
                active: header.headerComponent !== ""
                source: header.headerComponent
                onLoaded: item.trackId = Qt.binding(() => header.trackId)
            }

            ChromeButton {
                objectName: "track.showMenu:" + header.trackId
                actionId: "track.showMenu"
                params: ({track: header.trackId})
                iconName: "dots-three"
                iconColor: Theme.inkDim
                visible: hover.hovered || Nav.trackMenu === header.trackId
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
                Layout.preferredWidth: Theme.headerButton
            }
            FlagButton {
                trackId: header.trackId
                flag: "track.setMute"
                value: header.mute
                text: "M"
                activeColor: Theme.mute
            }
            FlagButton {
                trackId: header.trackId
                flag: "track.setSolo"
                value: header.solo
                text: "S"
            }
            FlagButton {
                opacity: header.records ? 1 : 0
                enabled: header.records
                trackId: header.trackId
                flag: "track.setArm"
                value: header.armed
                text: "R"
                activeColor: Theme.rec
            }
            FlagButton {
                opacity: header.records ? 1 : 0
                enabled: header.records
                trackId: header.trackId
                flag: "track.setMonitor"
                value: header.monitor
                text: "I"
            }
        }

        RowLayout {
            visible: header.roomy && header.showsAutomation
            spacing: Theme.space1
            Layout.fillWidth: true
            Layout.leftMargin: Theme.iconSize + Theme.space2

            AutomationPicker {
                actionId: "automation.setMode"
                trackId: header.trackId
                current: header.automationModes.find(mode => mode.id === header.automationMode)?.title ?? ""
                choices: header.automationModes
                active: header.automationMode !== "off" && header.automationMode !== "read"
                activeColor: Theme.tape
            }
            AutomationPicker {
                actionId: "automation.showLane"
                trackId: header.trackId
                current: header.automationParams.find(param => param.id === header.automationParam)?.title ?? ""
                choices: header.automationParams.map(param => ({id: param.id, title: param.title,
                                                               params: {param: param.id}}))
                Layout.fillWidth: true
            }
        }

        RowLayout {
            visible: header.roomy && !header.showsAutomation
            spacing: Theme.space2
            Layout.fillWidth: true
            Layout.leftMargin: Theme.iconSize + Theme.space2

            GainSlider {
                trackId: header.trackId
                gainDb: header.gainDb
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.fadeHandle + Theme.space1
            }

            GainReadout {
                trackId: header.trackId
                gainDb: header.gainDb
            }

            PanKnob {
                trackId: header.trackId
                pan: header.pan
            }
        }
    }

    LevelMeter {
        id: meter
        trackId: header.trackId
        input: header.armed || header.monitor
        anchors.right: parent.right
        anchors.rightMargin: Theme.hairline
        height: parent.height
    }

    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline
        height: parent.height
        color: Theme.rule
    }
}
