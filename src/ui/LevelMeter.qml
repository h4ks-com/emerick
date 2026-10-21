import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A track's peak meter: one bar per channel rising from green through yellow to red at full scale, a held peak tick
// and a clip light that stays lit until clicked.
Item {
    id: meter
    required property string trackId
    property bool input: false
    readonly property var level: input ? Meters.input(trackId) : Meters.output(trackId)

    visible: level.channels.length > 0
    implicitWidth: level.channels.length * (Theme.meterBar + Theme.meterGap) + Theme.meterGap

    Gradient {
        id: heat
        GradientStop { position: 0; color: Theme.rec }
        GradientStop { position: 1 - Meters.fraction(Theme.meterHotDb); color: Theme.meterHot }
        GradientStop { position: 1 - Meters.fraction(Theme.meterSafeDb); color: Theme.meterSafe }
        GradientStop { position: 1; color: Theme.meterSafe }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.graphite
    }

    Rectangle {
        id: clipLight
        x: Theme.meterGap
        y: Theme.meterGap
        width: parent.width - 2 * Theme.meterGap
        height: Theme.meterClipLight
        color: meter.level.clipped ? Theme.rec : Theme.lift
    }

    Row {
        x: Theme.meterGap
        y: clipLight.y + clipLight.height + Theme.meterGap
        height: parent.height - y - Theme.meterGap
        spacing: Theme.meterGap

        Repeater {
            model: meter.level.channels

            Rectangle {
                required property var modelData
                width: Theme.meterBar
                height: parent.height
                gradient: heat

                Rectangle {
                    width: parent.width
                    height: parent.height * (1 - modelData.level)
                    color: Theme.graphite
                }
                Rectangle {
                    visible: modelData.hold > 0
                    y: Math.min(parent.height * (1 - modelData.hold), parent.height - height)
                    width: parent.width
                    height: Theme.hairline
                    color: Theme.ink
                }
            }
        }
    }

    MouseArea {
        id: area
        objectName: "meters.resetClips:" + meter.trackId
        anchors.fill: parent
        hoverEnabled: true
        onClicked: Registry.runAction("meters.resetClips", {track: meter.trackId})
        ToolTip.visible: containsMouse
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.text: containsMouse
            ? (meter.input ? "Input" : "Output") + " peak "
                + (meter.level.peakDb > Meters.floorDb ? meter.level.peakDb.toFixed(1) + " dB" : "below " + Meters.floorDb + " dB")
                + (meter.level.clipped ? ", click to reset the clip light" : "")
            : ""
    }
}
