import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// The spectrogram: a waterfall that scrolls down from the newest moment, the newest spectrum with its decaying peaks
// above it, and the frequency, note and level under the pointer.
Window {
    id: window
    objectName: "window.spectrogram"
    title: "Spectrogram"
    width: Math.max(Theme.spectrogramWidth, minimumWidth)
    height: Theme.spectrogramHeight
    minimumWidth: controls.implicitWidth + 2 * Theme.space2
    color: Theme.graphite
    visible: true
    onClosing: Nav.spectrogramOpen = false

    readonly property var settings: Registry.settingsAt("settings.monitors")
    function setting(id) {
        return settings.find(entry => entry.id === id)
    }

    component SettingChoice: SearchCombo {
        readonly property var entry: window.setting(objectName)
        toolTip: entry.title
        model: entry.choices.map(choice => ({id: String(choice), name: String(choice)}))
        currentId: String(Registry.settings[objectName])
        onPicked: picked => Registry.setSettingValue(objectName, picked.id)
        implicitWidth: Theme.chordPickerWidth * 1.4
    }
    component SettingLevel: DragNumber {
        readonly property var entry: window.setting(objectName)
        toolTip: entry.title
        value: Registry.settings[objectName]
        from: entry.minimum
        to: entry.maximum
        unit: "dB"
        onCommitted: number => Registry.setSettingValue(objectName, number)
        implicitWidth: Theme.channelPickerWidth * 1.3
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            id: controls
            spacing: Theme.space2
            Layout.fillWidth: true
            Layout.margins: Theme.space2
            SearchCombo {
                objectName: "spectrogram.setSource"
                toolTip: Registry.actionInfo("spectrogram.setSource").title
                model: Spectrogram.sources
                currentId: Spectrogram.sourceId
                onPicked: entry => Registry.runAction("spectrogram.setSource", {source: entry.id})
                implicitWidth: Theme.menuWidth
            }
            SettingChoice {
                objectName: "spectrogram.fftSize"
            }
            SettingChoice {
                objectName: "spectrogram.window"
            }
            SettingChoice {
                objectName: "spectrogram.frequencyAxis"
            }
            SettingLevel {
                objectName: "spectrogram.floorDb"
            }
            SettingLevel {
                objectName: "spectrogram.ceilingDb"
            }
            CheckBox {
                id: spectrumToggle
                objectName: "spectrogram.showSpectrum"
                text: "Show spectrum"
                checked: Registry.settings[objectName]
                onToggled: Registry.setSettingValue(objectName, checked)
                contentItem: Label {
                    leftPadding: spectrumToggle.indicator.width + spectrumToggle.spacing
                    text: spectrumToggle.text
                    color: Theme.ink
                    font.pixelSize: Theme.textLabel
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            SpectrogramView {
                id: view
                objectName: "spectrogram.view"
                anchors.fill: parent
                anchors.bottomMargin: Theme.axisHeight
                monitor: Spectrogram
                spectrumHeight: Theme.spectrumHeight
                lineColor: Theme.spectrumLine
                peakColor: Theme.spectrumPeak
                bandColor: Theme.spectrumBand
            }

            Repeater {
                model: view.frequencyTicks
                Item {
                    required property var modelData
                    Rectangle {
                        x: modelData.x
                        y: view.waterfallTop
                        width: Theme.hairline
                        height: view.height - view.waterfallTop
                        color: Theme.barLine
                    }
                    Label {
                        x: Math.min(Math.max(0, modelData.x - width / 2), view.width - width)
                        y: view.height + (Theme.axisHeight - height) / 2
                        text: modelData.label
                        color: Theme.inkDim
                        font.pixelSize: Theme.textSmall
                        font.features: {"tnum": 1}
                    }
                }
            }
            Repeater {
                model: view.timeTicks.concat(view.levelTicks)
                Label {
                    required property var modelData
                    x: Theme.space1
                    y: modelData.y - height / 2
                    leftPadding: Theme.space1
                    rightPadding: Theme.space1
                    text: modelData.label
                    color: Theme.ink
                    font.pixelSize: Theme.textSmall
                    font.features: {"tnum": 1}
                    background: Rectangle {
                        radius: Theme.radiusClip / 2
                        color: Theme.axisShade
                    }
                }
            }

            Label {
                anchors.centerIn: view
                visible: view.rowsDrawn === 0
                text: "Play the project or a live input to see " + (Spectrogram.sourceName || "the master")
                color: Theme.inkDim
                font.pixelSize: Theme.textBody
            }

            MouseArea {
                id: hover
                anchors.fill: view
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                property var reading: ({})
                onPositionChanged: mouse => reading = view.readout(mouse.x, mouse.y)
                onExited: reading = {}
            }
            Label {
                objectName: "spectrogram.readout"
                anchors.top: view.top
                anchors.right: view.right
                anchors.margins: Theme.space2
                visible: hover.containsMouse && hover.reading.hz !== undefined
                leftPadding: Theme.space2
                rightPadding: Theme.space2
                topPadding: Theme.space1
                bottomPadding: Theme.space1
                text: {
                    const reading = hover.reading
                    if (reading.hz === undefined)
                        return ""
                    const hz = reading.hz >= 1000 ? (reading.hz / 1000).toFixed(2) + " kHz" : reading.hz.toFixed(1) + " Hz"
                    const db = reading.db !== undefined ? "   " + reading.db.toFixed(1) + " dB" : ""
                    const age = reading.seconds !== undefined ? "   " + reading.seconds.toFixed(1) + " s ago" : ""
                    return hz + "   " + reading.note + db + age
                }
                color: Theme.ink
                font.pixelSize: Theme.textLabel
                font.features: {"tnum": 1}
                background: Rectangle {
                    radius: Theme.radiusClip
                    color: Theme.axisShade
                }
            }
        }
    }
}
