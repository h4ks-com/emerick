import QtQuick
import QtQuick.Layouts
import Emerick.App

// A track's further automation lane in its own row below the track: a header that picks or hides the lane, and the lane.
Item {
    id: automationRow
    required property real headerWidth
    required property string trackId
    required property color trackColor
    required property var param
    // Every parameter the track can automate, as the track list describes them.
    required property var params
    required property var points
    required property bool reads
    required property real scrollTicks
    required property real pixelsPerTick

    Rectangle {
        width: automationRow.headerWidth
        height: parent.height
        color: Theme.slate

        Rectangle {
            width: Theme.colorStripe
            height: parent.height
            color: automationRow.trackColor
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.colorStripe + Theme.space2 + Theme.iconSize + Theme.space2
            anchors.rightMargin: Theme.space2
            spacing: Theme.space1

            AutomationPicker {
                objectName: "automation.showLane:" + automationRow.trackId + ":" + automationRow.param.id
                actionId: "automation.showLane"
                trackId: automationRow.trackId
                current: automationRow.param.title
                choices: automationRow.params.map(param => ({id: param.id, title: param.title,
                                                             params: {param: param.id, replace: automationRow.param.id}}))
                Layout.fillWidth: true
            }
            ChromeButton {
                objectName: "automation.hideLane:" + automationRow.trackId + ":" + automationRow.param.id
                actionId: "automation.hideLane"
                params: ({track: automationRow.trackId, param: automationRow.param.id})
                iconName: "x"
                iconColor: Theme.inkDim
                implicitWidth: Theme.headerButton
                implicitHeight: Theme.headerButton
                Layout.preferredWidth: Theme.headerButton
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: Theme.hairline
            height: parent.height
            color: Theme.rule
        }
    }

    AutomationLane {
        x: automationRow.headerWidth
        width: parent.width - automationRow.headerWidth
        height: parent.height
        clip: true
        primary: false
        trackId: automationRow.trackId
        trackColor: automationRow.trackColor
        param: automationRow.param
        points: automationRow.points
        reads: automationRow.reads
        scrollTicks: automationRow.scrollTicks
        pixelsPerTick: automationRow.pixelsPerTick
    }

    Rectangle {
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }
}
