import QtQuick
import Emerick.App

// The key of every action the registry lists, live anywhere in the window except while a text field has focus.
Item {
    id: keys
    Instantiator {
        model: Registry.shortcuts
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.shortcut
            context: Qt.ApplicationShortcut
            enabled: !(keys.Window.activeFocusItem instanceof TextInput)
            onActivated: Registry.activate(modelData.id)
        }
    }
}
