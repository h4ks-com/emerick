import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

ComboBox {
    required property string actionId
    required property string trackId
    required property var choices
    objectName: actionId + ":" + trackId
    Layout.fillWidth: true
    model: choices
    textRole: "label"
    onActivated: index => Registry.runAction(actionId, Object.assign({track: trackId}, choices[index].params))
}
