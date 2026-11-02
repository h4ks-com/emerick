import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The tuner: a needle over a cents scale from -50 to +50 with the in tune zone around 0, the note it hears, and how far
// off it sits.
Window {
    id: window
    objectName: "window.tuner"
    title: "Tuner"
    width: Theme.tunerWidth
    height: Theme.tunerHeight
    minimumWidth: Theme.tunerWidth
    minimumHeight: Theme.tunerHeight
    color: Theme.graphite
    visible: true
    onClosing: Nav.tunerOpen = false

    readonly property bool inTune: Tuner.shown && Math.abs(Tuner.cents) <= Theme.tunerInTuneCents
    readonly property color needleColor: !Tuner.shown ? Theme.inkDim
                                       : inTune ? Theme.meterSafe
                                       : Math.abs(Tuner.cents) <= Theme.tunerCloseCents ? Theme.meterHot : Theme.ink

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space4
        spacing: Theme.space3

        RowLayout {
            spacing: Theme.space2
            Layout.fillWidth: true
            SearchCombo {
                objectName: "tuner.setSource"
                toolTip: Registry.actionInfo("tuner.setSource").title
                model: Tuner.sources
                currentId: Tuner.sourceId
                onPicked: entry => Registry.runAction("tuner.setSource", {source: entry.id})
                Layout.fillWidth: true
            }
            DragNumber {
                objectName: "tuner.referencePitch"
                toolTip: "Set reference pitch of A4"
                value: Registry.settings["tuner.referencePitch"]
                from: Theme.referenceMinHz
                to: Theme.referenceMaxHz
                step: 0.5
                decimals: 1
                textFromValue: number => "A " + number.toFixed(1) + " Hz"
                onCommitted: number => Registry.setSettingValue(objectName, number)
                implicitWidth: Theme.channelPickerWidth * 1.5
            }
        }

        Item {
            id: gauge
            objectName: "tuner.gauge"
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property real radius: Math.min(width / 2, height) - Theme.space4
            readonly property point pivot: Qt.point(width / 2, height - Theme.space2)

            Canvas {
                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const context = getContext("2d")
                    const half = Theme.tunerSweep / 2 * Math.PI / 180
                    const top = -Math.PI / 2
                    const angleOf = cents => top + cents / Theme.tunerRangeCents * half
                    context.reset()
                    context.lineWidth = Theme.tunerArc
                    context.strokeStyle = Theme.rule
                    context.beginPath()
                    context.arc(gauge.pivot.x, gauge.pivot.y, gauge.radius, top - half, top + half)
                    context.stroke()
                    context.strokeStyle = Theme.meterSafe
                    context.beginPath()
                    context.arc(gauge.pivot.x, gauge.pivot.y, gauge.radius, angleOf(-Theme.tunerInTuneCents),
                                angleOf(Theme.tunerInTuneCents))
                    context.stroke()
                    context.lineWidth = Theme.hairline * 2
                    context.strokeStyle = Theme.inkDim
                    for (let cents = -Theme.tunerRangeCents; cents <= Theme.tunerRangeCents; cents += 10) {
                        const angle = angleOf(cents)
                        const inner = gauge.radius - Theme.tunerArc - Theme.tunerTick * (cents % 50 === 0 ? 1.6 : 1)
                        context.beginPath()
                        context.moveTo(gauge.pivot.x + Math.cos(angle) * inner, gauge.pivot.y + Math.sin(angle) * inner)
                        context.lineTo(gauge.pivot.x + Math.cos(angle) * (gauge.radius - Theme.tunerArc),
                                       gauge.pivot.y + Math.sin(angle) * (gauge.radius - Theme.tunerArc))
                        context.stroke()
                    }
                }
            }

            Repeater {
                model: [-50, 0, 50]
                Label {
                    required property int modelData
                    readonly property real angle: (-90 + modelData / Theme.tunerRangeCents * Theme.tunerSweep / 2) * Math.PI / 180
                    readonly property real reach: gauge.radius + Theme.space3
                    x: gauge.pivot.x + Math.cos(angle) * reach - width / 2
                    y: gauge.pivot.y + Math.sin(angle) * reach - height / 2
                    text: modelData > 0 ? "+" + modelData : String(modelData)
                    color: Theme.inkDim
                    font.pixelSize: Theme.textSmall
                    font.features: {"tnum": 1}
                }
            }

            Rectangle {
                objectName: "tuner.needle"
                width: Theme.tunerNeedle
                height: gauge.radius - Theme.tunerArc
                radius: width / 2
                x: gauge.pivot.x - width / 2
                y: gauge.pivot.y - height
                color: window.needleColor
                opacity: Tuner.holding ? Theme.mutedOpacity : 1
                transformOrigin: Item.Bottom
                rotation: Math.max(-1, Math.min(1, Tuner.cents / Theme.tunerRangeCents)) * Theme.tunerSweep / 2
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: noteName.implicitHeight
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                Label {
                    id: noteName
                    objectName: "tuner.note"
                    text: Tuner.shown ? Tuner.note : ""
                    color: window.inTune ? Theme.meterSafe : Theme.ink
                    font.pixelSize: Theme.tunerNote
                    font.weight: Font.Light
                }
                Label {
                    anchors.baseline: noteName.baseline
                    text: Tuner.shown ? String(Tuner.octave) : ""
                    color: Theme.inkDim
                    font.pixelSize: Theme.textTitle * 1.5
                }
            }
            Label {
                anchors.centerIn: parent
                visible: !Tuner.shown
                text: Tuner.sourceName === "" ? "Select a track to tune" : "Play a note on " + Tuner.sourceName
                color: Theme.inkDim
                font.pixelSize: Theme.textBody
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                objectName: "tuner.cents"
                text: Tuner.shown ? (Tuner.cents > 0 ? "+" : "") + Tuner.cents.toFixed(1) + " cents" : "- cents"
                color: Theme.ink
                font.pixelSize: Theme.textBody
                font.features: {"tnum": 1}
                Layout.fillWidth: true
            }
            Label {
                objectName: "tuner.hz"
                text: Tuner.shown ? Tuner.hz.toFixed(1) + " Hz" : "- Hz"
                color: Theme.inkDim
                font.pixelSize: Theme.textBody
                font.features: {"tnum": 1}
            }
        }
    }
}
