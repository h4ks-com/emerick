import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App
import Emerick.Ui
import Emerick.Yamaha

// The style a Style track's Arranger plays: its name, tempo and meter, the sections it has, and where to choose another.
ColumnLayout {
    id: styleInspector
    property string trackId
    readonly property var info: ArrangerStyles.revision >= 0 ? ArrangerStyles.info(trackId) : ({})
    // Each row hides on a track without the Arranger, so the empty column takes no room in the inspector.
    readonly property bool arranger: info.arranger === true
    spacing: Theme.space1

    Label {
        visible: styleInspector.arranger
        text: "Style"
        color: Theme.inkDim
        font.pixelSize: Theme.textLabel
        Layout.topMargin: Theme.space2
    }

    RowLayout {
        visible: styleInspector.arranger
        spacing: Theme.space1
        Layout.fillWidth: true
        // We read the style folders only while the picker is open, because listing them reads every style file.
        SearchCombo {
            id: stylePicker
            objectName: "arranger.style.load:" + styleInspector.trackId
            model: stylePicker.popup.visible ? ArrangerStyles.choices() : []
            currentId: styleInspector.info.path ?? ""
            missingText: styleInspector.info.name || "Choose style"
            toolTip: Registry.actionInfo("arranger.style.load").title
            onPicked: entry => Registry.runAction("arranger.style.load", {track: styleInspector.trackId, path: entry.id})
            Layout.fillWidth: true
        }
        ChromeButton {
            objectName: "arranger.style.load:" + styleInspector.trackId + ":file"
            actionId: "arranger.style.load"
            iconName: "file"
            hint: "Open a style file"
            implicitWidth: Theme.headerButton
            implicitHeight: Theme.headerButton
        }
        WarningBadge {
            objectName: "arranger.style.warning:" + styleInspector.trackId
            message: styleInspector.info.warning ?? ""
        }
    }

    RowLayout {
        visible: styleInspector.info.loaded === true
        spacing: Theme.space1
        Label {
            objectName: "arranger.style.tempo:" + styleInspector.trackId
            text: styleInspector.info.loaded ? styleInspector.info.bpm + " BPM   " + styleInspector.info.meter : ""
            color: Theme.ink
            font.pixelSize: Theme.textLabel
            font.features: {"tnum": 1}
        }
        WarningBadge {
            objectName: "project.setTempo:" + styleInspector.trackId + ":style"
            message: styleInspector.info.tempoWarning ?? ""
            onClicked: Registry.runAction("project.setTempo", {bpm: styleInspector.info.bpm})
        }
    }

    Flow {
        objectName: "arranger.style.sections:" + styleInspector.trackId
        visible: styleInspector.info.loaded === true
        spacing: Theme.space2
        Layout.fillWidth: true
        Repeater {
            model: styleInspector.info.sections ?? []
            Row {
                id: slot
                required property var modelData
                spacing: Theme.space2
                Label {
                    visible: slot.modelData.separated
                    text: "·"
                    color: Theme.inkDim
                    font.pixelSize: Theme.textLabel
                }
                Label {
                    objectName: "arranger.style.section:" + styleInspector.trackId + ":" + slot.modelData.label
                    text: slot.modelData.label
                    color: slot.modelData.present ? Theme.ink : Theme.inkDim
                    opacity: slot.modelData.present ? 1 : 0.5
                    font.pixelSize: Theme.textLabel
                }
            }
        }
    }
}
