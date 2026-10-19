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

    component ChannelBox: ThemeSpinBox {
        required property string actionId
        required property string trackId
        objectName: actionId + ":" + trackId + ":channel"
        from: 0
        to: Project.midiChannelCount
        textFromValue: value => value === 0 ? "All" : value
        onValueModified: Registry.runAction(actionId, {track: trackId, channel: value})
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
            required property string audioInText
            required property string audioOutText
            required property string midiInText
            required property string midiOutText
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
            RouteCombo {
                visible: panel.audioRoutes
                actionId: "track.setAudioIn"
                trackId: panel.trackId
                choices: Project.routeChoices.audioIn
                displayText: panel.audioInText
            }
            InputMeter {
                trackId: panel.trackId
                Layout.fillWidth: true
            }
            RouteLabel { text: "Audio output"; visible: panel.audioRoutes }
            RouteCombo {
                visible: panel.audioRoutes
                actionId: "track.setAudioOut"
                trackId: panel.trackId
                choices: Project.routeChoices.audioOut
                displayText: panel.audioOutText
            }

            RouteLabel { text: "MIDI input"; visible: panel.midiRoutes }
            RowLayout {
                visible: panel.midiRoutes
                RouteCombo {
                    actionId: "track.setMidiIn"
                    trackId: panel.trackId
                    choices: Project.routeChoices.midiIn
                    displayText: panel.midiInText
                }
                ChannelBox {
                    actionId: "track.setMidiIn"
                    trackId: panel.trackId
                    value: panel.midiIn.channel
                }
            }
            RouteLabel { text: "MIDI output"; visible: panel.midiRoutes }
            RowLayout {
                visible: panel.midiRoutes
                RouteCombo {
                    actionId: "track.setMidiOut"
                    trackId: panel.trackId
                    choices: Project.routeChoices.midiOut
                    displayText: panel.midiOutText
                }
                ChannelBox {
                    actionId: "track.setMidiOut"
                    trackId: panel.trackId
                    value: panel.midiOut.channel
                }
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
