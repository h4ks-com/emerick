import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One registry action in a menu, run with params when it has them; an action with a file dialog asks for its file.
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
        // A native menu shows a check only on a checked action.
        checkable: item.marked
        checked: item.marked
        onTriggered: {
            // A checkable action flips itself when triggered, so we bind it to the registry's state again.
            checked = Qt.binding(() => item.marked)
            Registry.activate(item.actionId, item.params ?? {})
        }
    }
}
