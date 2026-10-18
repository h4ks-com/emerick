import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One registry action in a menu, run with params when it has them.
GlassMenuItem {
    id: item
    property string actionId
    property string title
    property var params
    property bool available: true
    property bool bindsShortcut: false

    action: Action {
        text: item.title
        enabled: item.available
        shortcut: item.bindsShortcut ? item.shortcut : ""
        onTriggered: item.params === undefined ? Registry.activate(item.actionId) : Registry.runAction(item.actionId, item.params)
    }
}
