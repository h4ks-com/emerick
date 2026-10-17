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

    component ChannelBox: SpinBox {
        required property string actionId
        required property string trackId
        objectName: actionId + ":" + trackId + ":channel"
        from: 0
        to: Project.midiChannelCount
        textFromValue: value => value === 0 ? "All" : value
        onValueModified: Registry.runAction(actionId, {track: trackId, channel: value})
    }

    Repeater {
        model: Tracks
        delegate: ColumnLayout {
            id: panel
            required property string trackId
            required property string name
            required property bool audioRoutes
            required property bool midiRoutes
            required property string audioInText
            required property string audioOutText
            required property string midiInText
            required property string midiOutText
            required property var midiIn
            required property var midiOut
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
