import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

Rectangle {
    id: inspector
    objectName: "inspector"
    color: Theme.slate

    component RouteLabel: Label {
        color: Theme.inkDim
        font.pixelSize: Theme.textLabel
        Layout.topMargin: Theme.space2
    }

    component AudioRoute: AudioRoutePicker {
        required property string actionId
        required property string trackId
        name: actionId + ":" + trackId
        onPicked: params => Registry.runAction(actionId, Object.assign({track: trackId}, params))
        Layout.fillWidth: true
    }

    component MidiRoute: RowLayout {
        id: midiRoute
        required property string actionId
        required property string trackId
        required property var ports
        required property var route
        Layout.fillWidth: true
        RouteCombo {
            actionId: midiRoute.actionId
            trackId: midiRoute.trackId
            model: midiRoute.ports
            currentId: midiRoute.route.port
            Layout.fillWidth: true
        }
        RouteCombo {
            actionId: midiRoute.actionId
            trackId: midiRoute.trackId
            part: "channel"
            model: Project.routeDevices.midiChannels
            currentId: String(midiRoute.route.channel)
            Layout.preferredWidth: Theme.channelPickerWidth
        }
    }

    // A clip number that, with several clips selected, applies to every one of them as one undo step.
    component ClipNumber: DragNumber {
        required property var clipPanel
        // Off for a field whose commit runs elsewhere, such as in LinkedFields.
        property bool appliesItself: true
        objectName: actionId + ":" + (clipPanel.clip ? clipPanel.clip.id : "") + ":" + param
        badge: Nav.selectedClips.length > 1 ? Nav.selectedClips.length + " clips" : ""
        Layout.fillWidth: true
        onCommitted: number => {
            if (appliesItself) {
                const params = {}
                params[param] = number
                clipPanel.applyToClips(actionId, params)
            }
        }
    }

    component PositionNumber: ClipNumber {
        property bool duration: false
        step: Project.ticksPerBeat / 4
        from: 0
        textFromValue: ticks => Project.ticksText(Math.round(ticks), duration)
        valueFromText: text => {
            const ticks = Project.parseTicks(text, duration)
            return ticks < 0 ? NaN : ticks
        }
    }

    Repeater {
        model: Tracks
        delegate: ColumnLayout {
            id: panel
            required property string trackId
            required property string name
            required property var clips
            required property bool audioRoutes
            required property bool midiRoutes
            required property var audioIn
            required property var audioOut
            required property var midiIn
            required property var midiOut
            readonly property var clip: visible && Nav.selectedClips.length > 0
                                        ? clips.find(entry => entry.id === Nav.selectedClips[0]) : undefined
            readonly property bool audioClip: clip !== undefined && clip.type === "audio"
            readonly property bool severalClips: Nav.selectedClips.length > 1

            // Runs the action on the shown clip, or on every selected clip as one undo step.
            function applyToClips(actionId, params) {
                if (!severalClips) {
                    Registry.runAction(actionId, Object.assign({clip: clip.id}, params))
                    return
                }
                Registry.runActions(Registry.actionInfo(actionId).title, Nav.selectedClips.map(id => ({
                    id: actionId,
                    params: Object.assign({clip: id}, params)
                })))
            }
            visible: trackId === Nav.selectedTrack
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space1

            Label {
                text: panel.name
                color: Theme.ink
                font.pixelSize: Theme.textBody
                font.weight: Font.Medium
            }

            RouteLabel { text: "Audio input"; visible: panel.audioRoutes }
            AudioRoute {
                visible: panel.audioRoutes
                actionId: "track.setAudioIn"
                trackId: panel.trackId
                devices: Project.routeDevices.audioIn
                route: panel.audioIn
            }
            InputMeter {
                trackId: panel.trackId
                Layout.fillWidth: true
            }
            RouteLabel { text: "Audio output"; visible: panel.audioRoutes }
            AudioRoute {
                visible: panel.audioRoutes
                actionId: "track.setAudioOut"
                trackId: panel.trackId
                devices: Project.routeDevices.audioOut
                route: panel.audioOut
            }

            RouteLabel { text: "MIDI input"; visible: panel.midiRoutes }
            MidiRoute {
                visible: panel.midiRoutes
                actionId: "track.setMidiIn"
                trackId: panel.trackId
                ports: Project.routeDevices.midiIn
                route: panel.midiIn
            }
            RouteLabel { text: "MIDI output"; visible: panel.midiRoutes }
            MidiRoute {
                visible: panel.midiRoutes
                actionId: "track.setMidiOut"
                trackId: panel.trackId
                ports: Project.routeDevices.midiOut
                route: panel.midiOut
            }

            Loader {
                active: panel.clip !== undefined
                Layout.topMargin: Theme.space4
                Layout.fillWidth: true
                sourceComponent: clipProperties
            }

            Component {
                id: clipProperties
                GridLayout {
                columns: 2
                columnSpacing: Theme.space2
                rowSpacing: Theme.space1

                Label {
                    text: panel.severalClips ? Nav.selectedClips.length + " clips" : panel.clip ? "Clip, take " + panel.clip.take : ""
                    color: Theme.ink
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                    Layout.columnSpan: 2
                }
                RouteLabel { text: "Start" }
                PositionNumber {
                    clipPanel: panel
                    actionId: "clip.move"
                    param: "start"
                    appliesItself: false
                    value: panel.clip ? panel.clip.start : 0
                    // clip.move moves the whole selection by the same offset itself.
                    onCommitted: number => Registry.runAction("clip.move", {clip: panel.clip.id, start: number, selection: true})
                }
                RouteLabel { text: "End"; visible: !panel.severalClips }
                PositionNumber {
                    visible: !panel.severalClips
                    clipPanel: panel
                    actionId: "clip.resize"
                    param: "end"
                    from: panel.clip ? panel.clip.start + 1 : 0
                    value: panel.clip ? panel.clip.start + panel.clip.length : 0
                }
                RouteLabel { text: "Length" }
                PositionNumber {
                    clipPanel: panel
                    actionId: "clip.resize"
                    param: "length"
                    duration: true
                    from: 1
                    value: panel.clip ? panel.clip.length : 0
                }
                RowLayout {
                    visible: panel.audioClip
                    Layout.columnSpan: 2
                    Layout.topMargin: Theme.space2
                    RouteLabel {
                        text: "Fades"
                        Layout.topMargin: 0
                        Layout.fillWidth: true
                    }
                    ChromeButton {
                        objectName: "edit.toggleLinkFades"
                        active: Registry.settings["edit.linkFades"] === true
                        activeColor: Theme.inkDim
                        implicitHeight: Theme.headerButton
                    }
                }
                RouteLabel { text: "In"; visible: panel.audioClip }
                ClipNumber {
                    id: fadeIn
                    visible: panel.audioClip
                    clipPanel: panel
                    actionId: "clip.setFade"
                    param: "fadeIn"
                    appliesItself: false
                    from: 0
                    to: panel.clip ? panel.clip.length : 0
                    step: Project.ticksPerBeat / 16
                    unit: "ticks"
                    value: panel.audioClip ? panel.clip.fadeIn : 0
                }
                RouteLabel { text: "Out"; visible: panel.audioClip }
                ClipNumber {
                    id: fadeOut
                    visible: panel.audioClip
                    clipPanel: panel
                    actionId: "clip.setFade"
                    param: "fadeOut"
                    appliesItself: false
                    from: 0
                    to: panel.clip ? panel.clip.length : 0
                    step: Project.ticksPerBeat / 16
                    unit: "ticks"
                    value: panel.audioClip ? panel.clip.fadeOut : 0
                }
                LinkedFields {
                    fields: [fadeIn, fadeOut]
                    linked: Registry.settings["edit.linkFades"] === true
                    onCommitted: params => panel.applyToClips("clip.setFade", params)
                }
                RouteLabel { text: "Gain"; visible: panel.audioClip }
                ClipNumber {
                    visible: panel.audioClip
                    clipPanel: panel
                    actionId: "clip.setGain"
                    param: "db"
                    from: Theme.gainMinDb
                    to: Theme.clipGainMaxDb
                    step: Theme.gainStepDb
                    decimals: 1
                    unit: "dB"
                    value: panel.audioClip ? panel.clip.gainDb : 0
                }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: Nav.selectedTrack === ""
        text: "Select a track to see its routes"
        color: Theme.inkDim
        font.pixelSize: Theme.textLabel
    }
}
