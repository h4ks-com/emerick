import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models
import Emerick.App

// A menu built from one registry menu; an action with a submenu lists its choices there.
GlassMenu {
    id: menu
    required property string menuName

    Instantiator {
        model: Registry.menus[menu.menuName]
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
                }
            }
            DelegateChoice {
                roleValue: App.Submenu
                GlassMenu {
                    id: submenu
                    required property var modelData
                    objectName: modelData.id
                    title: modelData.title
                    Instantiator {
                        model: submenu.modelData.choices
                        delegate: ActionMenuItem {
                            required property var modelData
                            objectName: submenu.modelData.id + ":" + modelData.id
                            actionId: submenu.modelData.id
                            title: modelData.title
                            params: modelData.params
                        }
                        onObjectAdded: (index, object) => submenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => submenu.removeItem(object)
                    }
                }
            }
        }
        onObjectAdded: (index, object) => object instanceof Menu ? menu.insertMenu(index, object) : menu.insertItem(index, object)
        onObjectRemoved: (index, object) => object instanceof Menu ? menu.removeMenu(object) : menu.removeItem(object)
    }
}
