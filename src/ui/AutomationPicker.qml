import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// A button that shows the current choice and opens a menu that runs the action with the picked choice's params.
ChromeButton {
    id: picker
    required property string trackId
    // Each choice is {id, title, params}.
    required property var choices
    property string current

    objectName: actionId + ":" + trackId
    text: current
    menu: choiceMenu
    padding: Theme.space1
    implicitHeight: Theme.headerButton

    GlassMenu {
        id: choiceMenu
        objectName: picker.objectName + ":menu"
        marks: true

        Instantiator {
            model: picker.choices
            delegate: ActionMenuItem {
                required property var modelData
                objectName: picker.objectName + ":" + modelData.id
                actionId: picker.actionId
                title: modelData.title
                marked: modelData.title === picker.current
                params: Object.assign({track: picker.trackId}, modelData.params)
            }
            onObjectAdded: (index, object) => choiceMenu.insertItem(index, object)
        }
    }
}
