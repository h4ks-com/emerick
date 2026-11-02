import QtQuick
import Emerick.App
import Emerick.Ui

TrackList {
    focus: true

    Repeater {
        model: ["transport.togglePlay", "view.toggleAutomation", "marker.previous", "marker.next"]
        delegate: Item {
            required property string modelData
            Shortcut {
                sequence: Registry.actionInfo(parent.modelData).shortcut
                onActivated: Registry.activate(parent.modelData)
            }
        }
    }
}
