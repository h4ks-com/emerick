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
    readonly property var bands: result?.bands ?? []
    readonly property real loudestShare: bands.reduce((loudest, band) => Math.max(loudest, band.share), 0.001)
    spacing: Theme.space2

    function level(value) {
        return value === null || value === undefined ? "-inf" : value.toFixed(1)
    }

    function frequency(hz) {
        return hz >= 1000 ? hz / 1000 + " kHz" : hz + " Hz"
    }

    function edges(band) {
        return band.lowHz === 0 ? "below " + frequency(band.highHz)
             : band.highHz === null ? "above " + frequency(band.lowHz)
             : frequency(band.lowHz) + " to " + frequency(band.highHz)
    }

    component Reading: Row {
        id: reading
        required property var value
        required property string unit
        required property string tip
        spacing: Theme.space1
        Label {
            objectName: "track.analyze:" + row.trackId + ":" + reading.unit
            text: row.level(reading.value)
            color: Theme.ink
            font.pixelSize: Theme.textSmall
            font.features: {"tnum": 1}
        }
        Label {
            text: reading.unit
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
        }
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

    ColumnLayout {
        objectName: "track.analyze:" + row.trackId + ":readout"
        visible: row.result !== undefined
        opacity: row.busy ? Theme.mutedOpacity : 1
        spacing: Theme.space1
        RowLayout {
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
        }
        Row {
            id: bandBars
            spacing: Theme.meterGap
            Repeater {
                model: row.bands
                Item {
                    required property var modelData
                    width: Theme.meterBar * 2
                    height: Theme.iconSize
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.max(Theme.hairline, parent.height * parent.modelData.share / row.loudestShare)
                        color: Theme.inkDim
                    }
                }
            }
            HoverHandler { id: bandsHover }
            ToolTip.text: row.bands.map(band => band.name + ", " + row.edges(band) + ": "
                              + Math.round(100 * band.share) + "%").join("\n")
                          + "\nCentroid: " + Math.round(row.result?.centroidHz ?? 0) + " Hz"
            ToolTip.visible: bandsHover.hovered
            ToolTip.delay: Theme.tooltipDelay
        }
    }
}
