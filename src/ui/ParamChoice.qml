import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// An insert param that picks one of a few values, such as a delay's note value, from a menu.
ChromeButton {
    id: choice
    required property string insertId
    required property var param
    required property real paramValue
    objectName: "insert.setParam:" + insertId + ":" + param.id
    actionId: "insert.setParam"
    text: param.choices[Math.round(paramValue)] ?? ""
    hint: param.title
    menu: choices
    implicitHeight: Theme.headerButton

    GlassMenu {
        id: choices
        objectName: choice.objectName + ":menu"
        Instantiator {
            model: choice.param.choices
            delegate: ActionMenuItem {
                required property string modelData
                required property int index
                objectName: choice.objectName + ":" + index
                actionId: "insert.setParam"
                title: modelData
                params: ({insert: choice.insertId, param: choice.param.id, value: index})
            }
            onObjectAdded: (index, object) => choices.insertItem(index, object)
            onObjectRemoved: (index, object) => choices.removeItem(object)
        }
    }
}
