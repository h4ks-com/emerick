import QtQuick
import QtQuick.Layouts

// A device picker and, once a device is set, the picker of its channels. Picking hands over only the params that
// entry sets, so the receiver keeps the rest of the route.
RowLayout {
    id: picker
    // The device picker's objectName; the channel picker appends ":channel".
    required property string name
    required property var devices
    required property var route
    readonly property var device: devices.find(entry => entry.id === route.device)

    signal picked(var params)

    SearchCombo {
        objectName: picker.name
        model: picker.devices
        currentId: picker.route.device
        missingText: "Missing device"
        onPicked: entry => picker.picked(entry.params)
        Layout.fillWidth: true
    }
    SearchCombo {
        objectName: picker.name + ":channel"
        visible: picker.route.device !== ""
        model: picker.device !== undefined ? picker.device.channels : []
        currentId: picker.route.channels.join("-")
        onPicked: entry => picker.picked(entry.params)
        Layout.preferredWidth: Theme.channelPickerWidth
    }
}
