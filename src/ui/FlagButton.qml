import QtQuick
import QtQuick.Layouts
import Emerick.App

// A track toggle such as mute, which a ToggleSweep drag can set across tracks.
ChromeButton {
    required property string trackId
    required property string flag
    required property bool value
    readonly property string sweepGroup: flag
    readonly property string sweepActionId: flag
    readonly property var sweepParams: ({track: trackId, selection: true})
    property int sweepState: -1
    objectName: flag + ":" + trackId
    actionId: flag
    params: ({track: trackId, value: !value, selection: true})
    active: sweepState >= 0 ? sweepState === 1 : value
    implicitWidth: Theme.headerButton
    implicitHeight: Theme.headerButton
    padding: 0
    Layout.preferredWidth: Theme.headerButton
}
