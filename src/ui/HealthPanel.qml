import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Popup {
    id: panel
    anchors.centerIn: parent
    width: Theme.paletteWidth
    padding: Theme.space4
    modal: true
    visible: Nav.healthOpen
    onClosed: Nav.healthOpen = false
    background: GlassPanel {}
    contentItem: Column {
        objectName: "health"
        spacing: Theme.space2
        Label {
            text: "Project health"
            color: Theme.ink
            font.pixelSize: Theme.textTitle
        }
        Label {
            visible: Project.healthIssues.length === 0
            text: "No issues found"
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
        }
        Repeater {
            model: Project.healthIssues
            delegate: ChromeButton {
                required property var modelData
                objectName: modelData.clip ? "selection.set:" + modelData.clip : "track.select:" + modelData.track
                actionId: modelData.clip ? "selection.set" : "track.select"
                params: modelData.clip ? {clips: [modelData.clip]} : {track: modelData.track}
                text: modelData.message
                width: parent.width
                onClicked: panel.close()
            }
        }
        ChromeButton {
            objectName: "project.health"
        }
    }
}
