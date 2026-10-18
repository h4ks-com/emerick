import QtQuick
import QtQuick.Layouts
import Emerick.App

ThemeComboBox {
    required property string actionId
    required property string trackId
    required property var choices
    objectName: actionId + ":" + trackId
    Layout.fillWidth: true
    model: choices
    textRole: "label"
    // The inspector shows the route as displayText, so no choice starts highlighted.
    currentIndex: -1
    onActivated: index => Registry.runAction(actionId, Object.assign({track: trackId}, choices[index].params))
}
