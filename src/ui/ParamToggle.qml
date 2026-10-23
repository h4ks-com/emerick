import QtQuick
import Emerick.App

// An insert param that is on or off, such as an EQ band or a synced delay.
ChromeButton {
    required property string insertId
    required property var param
    required property real paramValue
    objectName: "insert.setParam:" + insertId + ":" + param.id
    actionId: "insert.setParam"
    params: ({insert: insertId, param: param.id, value: paramValue >= 0.5 ? 0 : 1})
    active: paramValue >= 0.5
    activeColor: Theme.ink
    text: param.title
    hint: param.title
    implicitHeight: Theme.headerButton
}
