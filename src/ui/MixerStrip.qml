import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// One track's channel strip: its output, inserts, sends, trim and pan knobs, gain fader with level meter, mute, solo
// and arm, and its name.
Rectangle {
    id: strip
    required property string trackId
    required property string name
    required property int colorIndex
    required property bool mute
    required property bool solo
    required property bool armed
    required property bool monitor
    required property real gainDb
    required property real trimDb
    required property real pan
    required property bool midiRoutes
    required property bool busRoutes
    required property bool playsAudio
    required property var audioOut
    required property var midiOut
    required property var sends
    required property var inserts
    required property string outputBusName
    readonly property bool selected: Nav.selectedTrack === trackId
    readonly property string outputText: {
        if (outputBusName !== "") {
            return outputBusName
        }
        if (playsAudio) {
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

    implicitWidth: Registry.settings["view.stripWidth"]
    implicitHeight: column.implicitHeight + 2 * Theme.space1
    border.width: Theme.hairline
    border.color: selected ? Theme.inkDim : Theme.rule
    gradient: Gradient {
        GradientStop { position: 0; color: strip.selected ? Theme.rule : Theme.lift }
        GradientStop { position: 1; color: Theme.slate }
    }

    TapHandler {
        onTapped: Registry.runAction("track.select", {track: strip.trackId})
    }

    PaneEdge {
        settingId: "view.stripWidth"
        objectName: settingId + ":" + strip.trackId
        anchors.right: parent.right
        height: parent.height
    }

    ColumnLayout {
        id: column
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
            ToolTip.text: text
            ToolTip.visible: outputHover.hovered && truncated
            ToolTip.delay: Theme.tooltipDelay
            HoverHandler {
                id: outputHover
            }
            background: Rectangle {
                radius: Theme.radiusClip
                color: Theme.graphite
            }
        }

        InsertList {
            visible: strip.playsAudio
            trackId: strip.trackId
            inserts: strip.inserts
            Layout.fillWidth: true
        }

        // A strip without audio keeps the sends' room, so its pan knob and fader line up with the other strips'.
        SendList {
            opacity: strip.playsAudio ? 1 : 0
            enabled: strip.playsAudio
            trackId: strip.trackId
            sends: strip.sends
            compact: true
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.space2
            Layout.alignment: Qt.AlignHCenter
            TrimKnob {
                opacity: strip.playsAudio ? 1 : 0
                enabled: strip.playsAudio
                trackId: strip.trackId
                trimDb: strip.trimDb
            }
            PanKnob {
                trackId: strip.trackId
                pan: strip.pan
            }
        }

        RowLayout {
            spacing: Theme.space1
            Layout.fillHeight: true
            Layout.preferredHeight: Theme.faderMinHeight
            Layout.alignment: Qt.AlignHCenter
            GainSlider {
                orientation: Qt.Vertical
                trackId: strip.trackId
                gainDb: strip.gainDb
                Layout.fillHeight: true
                Layout.preferredWidth: Theme.faderCapWidth
            }
            LevelMeter {
                trackId: strip.trackId
                input: strip.armed || strip.monitor
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

        AnalysisReading {
            readonly property var result: Analysis.results[strip.trackId]
            objectName: "tracks.analyze:" + strip.trackId
            visible: result !== undefined
            opacity: Analysis.busyTrack === strip.trackId ? Theme.mutedOpacity : 1
            value: result?.activeRmsDb ?? null
            unit: "RMS"
            tip: "Active RMS in dB: the median level of the parts that play. Integrated "
                 + level(result?.integratedLufs ?? null) + " LUFS, true peak " + level(result?.truePeakDbtp ?? null)
                 + " dBTP"
            Layout.alignment: Qt.AlignHCenter
        }

        RowLayout {
            spacing: Theme.space1
            Layout.alignment: Qt.AlignHCenter
            FlagButton {
                trackId: strip.trackId
                flag: "track.setMute"
                value: strip.mute
                text: "M"
                activeColor: Theme.mute
            }
            FlagButton {
                trackId: strip.trackId
                flag: "track.setSolo"
                value: strip.solo
                text: "S"
            }
            FlagButton {
                opacity: strip.busRoutes ? 0 : 1
                enabled: !strip.busRoutes
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
