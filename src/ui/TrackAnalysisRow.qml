import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The analyze button and the track's last loudness and tone balance: integrated LUFS, true peak, active RMS and the
// energy share of five bands from low to high.
RowLayout {
    id: row
    required property string trackId
    readonly property var result: Analysis.results[trackId]
    readonly property bool busy: Analysis.busyTrack === trackId
    readonly property var bandNames: ["low", "lowMid", "mid", "highMid", "high"]
    readonly property var bandTitles: ["Below 120 Hz", "120 to 500 Hz", "500 Hz to 2 kHz", "2 to 6 kHz", "Above 6 kHz"]
    spacing: Theme.space2

    function level(value) {
        return value === null || value === undefined ? "-inf" : value.toFixed(1)
    }

    component Reading: Label {
        id: reading
        required property var value
        required property string unit
        required property string tip
        objectName: "track.analyze:" + row.trackId + ":" + unit
        text: row.level(value) + " <font color='" + Theme.inkDim + "'>" + unit + "</font>"
        textFormat: Text.StyledText
        color: Theme.ink
        font.pixelSize: Theme.textSmall
        font.features: {"tnum": 1}
        HoverHandler { id: readingHover }
        ToolTip.text: tip
        ToolTip.visible: readingHover.hovered
        ToolTip.delay: Theme.tooltipDelay
    }

    ChromeButton {
        objectName: "track.analyze:" + row.trackId
        actionId: "track.analyze"
        iconName: "chart-bar"
        iconColor: Theme.inkDim
        active: row.busy
        activeColor: Theme.inkDim
        hint: row.busy ? "Wait while the track renders" : "Render the track alone and measure its loudness and tone"
        run: () => Analysis.analyze(row.trackId)
    }

    RowLayout {
        objectName: "track.analyze:" + row.trackId + ":readout"
        visible: row.result !== undefined
        opacity: row.busy ? Theme.mutedOpacity : 1
        spacing: Theme.space2
        Reading {
            value: row.result?.integratedLufs ?? null
            unit: "LUFS"
            tip: "Integrated loudness over the whole render. Streaming services play near -14 LUFS"
        }
        Reading {
            value: row.result?.truePeakDbtp ?? null
            unit: "TP"
            tip: "True peak in dBTP, between samples. Keep it below -1 dBTP"
        }
        Reading {
            value: row.result?.activeRmsDb ?? null
            unit: "RMS"
            tip: "Active RMS in dB: the median level of the parts that play, so silence leaves it alone"
        }
        Row {
            id: bands
            spacing: Theme.meterGap
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: row.bandNames
                Item {
                    required property string modelData
                    width: Theme.meterBar * 2
                    height: Theme.iconSize
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.max(Theme.hairline, parent.height * (row.result?.bandShare[parent.modelData] ?? 0))
                        color: Theme.inkDim
                    }
                }
            }
            HoverHandler { id: bandsHover }
            ToolTip.text: row.bandNames.map((name, index) => row.bandTitles[index] + ": "
                              + Math.round(100 * (row.result?.bandShare[name] ?? 0)) + "%").join("\n")
                          + "\nCentroid: " + Math.round(row.result?.centroidHz ?? 0) + " Hz"
            ToolTip.visible: bandsHover.hovered
            ToolTip.delay: Theme.tooltipDelay
        }
    }
}
