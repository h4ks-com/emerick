import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// An action's notice with the issues it found, such as mix.inspect's; a click selects the issue's track and moves the
// playhead to where it starts.
Popup {
    id: panel
    property string title
    property string message
    property var issues: []

    function show(title, message, issues) {
        panel.title = title
        panel.message = message
        panel.issues = issues
        open()
    }

    function trackLabel(id) {
        return id === "master" ? "Mix" : (Tracks.trackName(id) || id)
    }

    function describe(issue) {
        const parts = [issue.severity, issue.kind || issue.problem, issue.track ? trackLabel(issue.track) : ""]
        if (issue.from)
            parts.push(issue.from.bbt + " to " + issue.to.bbt)
        return parts.filter(part => part).join(", ") + ": " + (issue.detail || issue.message)
    }

    function goTo(issue) {
        if (issue.track && issue.track !== "master")
            Registry.runAction("track.select", {track: issue.track})
        if (issue.from)
            Registry.runAction("transport.locate", {at: issue.from.bbt})
    }

    anchors.centerIn: parent
    width: Theme.paletteWidth
    padding: Theme.space4
    focus: true
    background: GlassPanel {}
    contentItem: Column {
        objectName: "issues"
        spacing: Theme.space2
        Label {
            text: panel.title
            color: Theme.ink
            font.pixelSize: Theme.textTitle
        }
        Label {
            text: panel.message
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
        }
        ListView {
            width: parent.width
            height: Math.min(contentHeight, panel.parent ? panel.parent.height / 2 : contentHeight)
            clip: true
            spacing: Theme.space1
            model: panel.issues
            delegate: ChromeButton {
                required property var modelData
                required property int index
                objectName: "transport.locate:" + index
                actionId: "transport.locate"
                text: panel.describe(modelData)
                width: ListView.view.width
                run: () => panel.goTo(modelData)
            }
        }
    }
}
