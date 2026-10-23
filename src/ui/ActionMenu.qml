import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models
import Emerick.App

// A menu built from one registry menu; an action with a submenu lists its choices there, and a nested menu path
// shows as a submenu. A context menu sets params, such as the clicked track, and every item runs with them.
GlassMenu {
    id: actionMenu
    required property string menuName
    property var params

    // We refresh what changes with the project when the menu opens, because rebuilding a native menu loses its items.
    onAboutToShow: {
        for (let at = 0; at < count; ++at) {
            const item = itemAt(at)
            if (item instanceof ActionMenuItem) {
                item.available = Registry.actionInfo(item.actionId).enabled
            }
        }
    }

    function paramsWith(extra) {
        return actionMenu.params === undefined && extra === undefined ? undefined : Object.assign({}, actionMenu.params, extra)
    }

    Instantiator {
        model: Registry.menus[actionMenu.menuName]
        delegate: DelegateChooser {
            role: "row"
            DelegateChoice {
                roleValue: App.Command
                ActionMenuItem {
                    required property var modelData
                    objectName: modelData.id
                    actionId: modelData.id
                    title: modelData.title
                    shortcut: modelData.shortcut
                    available: modelData.enabled
                    bindsShortcut: modelData.bindsShortcut
                    params: actionMenu.paramsWith(undefined)
                }
            }
            DelegateChoice {
                roleValue: App.Submenu
                GlassMenu {
                    id: submenu
                    required property var modelData
                    property var choices: modelData.choices
                    objectName: modelData.id
                    title: modelData.title
                    onAboutToShow: {
                        const fresh = Registry.actionInfo(modelData.id).choices
                        if (JSON.stringify(fresh) !== JSON.stringify(choices)) {
                            choices = fresh
                        }
                    }
                    Instantiator {
                        model: submenu.choices
                        delegate: ActionMenuItem {
                            required property var modelData
                            objectName: submenu.modelData.id + ":" + modelData.id
                            actionId: submenu.modelData.id
                            title: modelData.title
                            params: actionMenu.paramsWith(modelData.params)
                        }
                        onObjectAdded: (index, object) => submenu.insertItem(index, object)
                    }
                }
            }
            DelegateChoice {
                roleValue: App.Menu
                Loader {
                    required property var modelData
                    Component.onCompleted: setSource("ActionMenu.qml", {objectName: "menu." + modelData.id,
                                                                        title: modelData.title,
                                                                        menuName: modelData.id})
                }
            }
        }
        onObjectAdded: (index, object) => {
            if (object instanceof Loader) {
                actionMenu.insertMenu(index, object.item)
            } else if (object instanceof Menu) {
                actionMenu.insertMenu(index, object)
            } else {
                actionMenu.insertItem(index, object)
            }
        }
        // A menu drops a destroyed item by itself, and removing it here as well empties a native menu.
        onObjectRemoved: (index, object) => {
            const menu = object instanceof Loader ? object.item : object
            for (let at = 0; menu instanceof Menu && at < actionMenu.count; ++at) {
                if (actionMenu.menuAt(at) === menu) {
                    actionMenu.takeMenu(at)
                    return
                }
            }
        }
    }
}
