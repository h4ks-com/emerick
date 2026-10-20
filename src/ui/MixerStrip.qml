import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// One track's channel strip: its output, gain fader with input meter, mute, solo and arm, and its name.
Rectangle {
    id: strip
    required property string trackId
    required property string name
    required property int colorIndex
    required property bool mute
    required property bool solo
    required property bool armed
    required property real gainDb
    required property bool audioRoutes
    required property bool midiRoutes
    required property var audioOut
    required property var midiOut
    readonly property bool selected: Nav.selectedTrack === trackId
    readonly property string outputText: {
        if (audioRoutes) {
            const device = Project.routeDevices.audioOut.find(entry => entry.id === audioOut.device)
            const deviceName = device !== undefined ? device.name : audioOut.device
            return audioOut.channels.length > 0 ? deviceName + " " + audioOut.channels.join("-") : deviceName
        }
        if (midiRoutes) {
            const port = Project.routeDevices.midiOut.find(entry => entry.id === midiOut.port)
            const portName = port !== undefined ? port.name : midiOut.port
            return midiOut.channel > 0 ? portName + " ch " + midiOut.channel : portName
        }
        return ""
    }

    implicitWidth: Theme.stripWidth
    border.width: Theme.hairline
    border.color: selected ? Theme.inkDim : Theme.rule
    gradient: Gradient {
        GradientStop { position: 0; color: strip.selected ? Theme.rule : Theme.lift }
        GradientStop { position: 1; color: Theme.slate }
    }

    TapHandler {
        onTapped: Registry.runAction("track.select", {track: strip.trackId})
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space1
        spacing: Theme.space1

        Label {
            objectName: "mixer.output:" + strip.trackId
            visible: strip.outputText !== ""
            text: strip.outputText
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            padding: Theme.space1
            Layout.fillWidth: true
            background: Rectangle {
                radius: Theme.radiusClip
                color: Theme.graphite
            }
        }

        RowLayout {
            spacing: Theme.space1
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignHCenter
            GainSlider {
                orientation: Qt.Vertical
                trackId: strip.trackId
                gainDb: strip.gainDb
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.faderCapWidth
            }
            InputMeter {
                vertical: true
                trackId: strip.trackId
                Layout.fillHeight: true
                Layout.topMargin: Theme.faderCapHeight / 2
                Layout.bottomMargin: Theme.faderCapHeight / 2
            }
        }

        GainReadout {
            trackId: strip.trackId
            gainDb: strip.gainDb
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.space1
            Layout.alignment: Qt.AlignHCenter
            FlagButton {
                trackId: strip.trackId
                flag: "track.setMute"
                value: strip.mute
                text: "M"
            }
            FlagButton {
                trackId: strip.trackId
                flag: "track.setSolo"
                value: strip.solo
                text: "S"
            }
            FlagButton {
                trackId: strip.trackId
                flag: "track.setArm"
                value: strip.armed
                text: "R"
                activeColor: Theme.rec
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.colorStripe
            radius: height / 2
            color: Theme.trackColors[strip.colorIndex % Project.trackColorCount]
        }
        Label {
            text: strip.name
            color: Theme.ink
            font.pixelSize: Theme.textLabel
            font.weight: Font.Medium
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
