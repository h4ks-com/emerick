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

    // A device picker and, once a device is set, the picker of its channels.
    component AudioRoute: RowLayout {
        id: audioRoute
        required property string actionId
        required property string trackId
        required property var devices
        required property var route
        readonly property var device: devices.find(entry => entry.id === route.device)
        Layout.fillWidth: true
        RouteCombo {
            actionId: audioRoute.actionId
            trackId: audioRoute.trackId
            model: audioRoute.devices
            currentId: audioRoute.route.device
            Layout.fillWidth: true
        }
        RouteCombo {
            visible: audioRoute.route.device !== ""
            actionId: audioRoute.actionId
            trackId: audioRoute.trackId
            part: "channel"
            model: audioRoute.device !== undefined ? audioRoute.device.channels : []
            currentId: audioRoute.route.channels.join("-")
            Layout.preferredWidth: Theme.channelPickerWidth
        }
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

    component ClipField: TextField {
        required property string actionId
        required property string field
        required property string clipId
        property bool numeric: false
        objectName: actionId + ":" + clipId + ":" + field
        color: Theme.ink
        font.pixelSize: Theme.textLabel
        font.features: {"tnum": 1}
        Layout.fillWidth: true
        background: Rectangle {
            color: Theme.lift
            radius: Theme.radiusClip
        }
        onEditingFinished: {
            const params = {clip: clipId}
            params[field] = numeric ? Number(text) : text
            Registry.runAction(actionId, params)
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
                    text: panel.clip ? "Clip, take " + panel.clip.take : ""
                    color: Theme.ink
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                    Layout.columnSpan: 2
                }
                RouteLabel { text: "Start" }
                ClipField {
                    actionId: "clip.move"
                    field: "start"
                    clipId: panel.clip ? panel.clip.id : ""
                    text: panel.clip ? panel.clip.startText : ""
                }
                RouteLabel { text: "End" }
                ClipField {
                    actionId: "clip.resize"
                    field: "end"
                    clipId: panel.clip ? panel.clip.id : ""
                    text: panel.clip ? panel.clip.endText : ""
                }
                RouteLabel { text: "Length" }
                Label {
                    text: panel.clip ? panel.clip.lengthText : ""
                    color: Theme.inkDim
                    font.pixelSize: Theme.textLabel
                    font.features: {"tnum": 1}
                    Layout.topMargin: Theme.space2
                }
                RouteLabel { text: "Fade in, ticks"; visible: panel.audioClip }
                ClipField {
                    visible: panel.audioClip
                    actionId: "clip.setFade"
                    field: "fadeIn"
                    numeric: true
                    clipId: panel.clip ? panel.clip.id : ""
                    text: panel.audioClip ? panel.clip.fadeIn : ""
                    validator: IntValidator { bottom: 0 }
                }
                RouteLabel { text: "Fade out, ticks"; visible: panel.audioClip }
                ClipField {
                    visible: panel.audioClip
                    actionId: "clip.setFade"
                    field: "fadeOut"
                    numeric: true
                    clipId: panel.clip ? panel.clip.id : ""
                    text: panel.audioClip ? panel.clip.fadeOut : ""
                    validator: IntValidator { bottom: 0 }
                }
                RouteLabel { text: "Gain, dB"; visible: panel.audioClip }
                ClipField {
                    visible: panel.audioClip
                    actionId: "clip.setGain"
                    field: "db"
                    numeric: true
                    clipId: panel.clip ? panel.clip.id : ""
                    text: panel.audioClip ? panel.clip.gainDb.toFixed(1) : ""
                    validator: DoubleValidator {}
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
