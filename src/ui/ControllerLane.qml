import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// One lane under the piano roll notes: a picker of what it shows beside the keys, and the note velocities or one
// controller's values over the grid.
Rectangle {
    id: laneItem

    required property PianoRoll roll
    required property string lane
    property bool last: false
    readonly property var choice: roll.laneChoices.find(entry => entry.id === lane)
                                  ?? {id: lane, title: lane, hasData: false, minimum: 0, maximum: 127, switched: false}

    color: Theme.slate

    Rectangle {
        width: parent.width
        height: Theme.hairline
        color: Theme.rule
    }

    Row {
        x: Theme.space2
        y: Theme.space1
        width: laneItem.roll.headerWidth - 2 * Theme.space2
        spacing: Theme.space1

        ChromeButton {
            objectName: "midi.changeLane:" + laneItem.lane
            actionId: "midi.changeLane"
            enabled: !Registry.readOnly
            width: Math.min(implicitWidth, parent.width - 2 * (Theme.headerButton + parent.spacing))
            text: laneItem.choice.title
            textSize: Theme.textSmall
            padding: Theme.space1
            implicitHeight: Theme.headerButton
            menu: GlassMenu {
                id: choiceMenu
                objectName: "midi.changeLane:" + laneItem.lane + ":menu"
                marks: true

                Instantiator {
                    model: laneItem.roll.laneChoices
                    delegate: ActionMenuItem {
                        required property var modelData
                        objectName: "midi.changeLane:" + laneItem.lane + ":" + modelData.id
                        actionId: "midi.changeLane"
                        title: modelData.title
                        marked: modelData.id === laneItem.lane
                        iconName: modelData.hasData ? "circle" : ""
                        params: ({lane: laneItem.lane, to: modelData.id})
                    }
                    onObjectAdded: (index, object) => choiceMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => choiceMenu.removeItem(object)
                }
            }
            hint: "Pick a controller to show here; dotted ones hold data in this clip"
        }
        ChromeButton {
            objectName: "midi.removeLane:" + laneItem.lane
            actionId: "midi.removeLane"
            visible: laneItem.roll.lanes.length > 1 && !Registry.readOnly
            implicitWidth: Theme.headerButton
            implicitHeight: Theme.headerButton
            iconName: "x"
            iconColor: Theme.inkDim
            params: ({lane: laneItem.lane})
        }
        ChromeButton {
            objectName: "midi.addLane"
            visible: laneItem.last && !Registry.readOnly
            implicitWidth: Theme.headerButton
            implicitHeight: Theme.headerButton
            iconName: "plus"
            iconColor: Theme.inkDim
            menu: GlassMenu {
                id: addMenu
                objectName: "midi.addLane:menu"

                Instantiator {
                    model: laneItem.roll.laneChoices.filter(entry => !laneItem.roll.lanes.includes(entry.id))
                    delegate: ActionMenuItem {
                        required property var modelData
                        objectName: "midi.addLane:" + modelData.id
                        actionId: "midi.addLane"
                        title: modelData.title
                        iconName: modelData.hasData ? "circle" : ""
                        params: ({lane: modelData.id})
                    }
                    onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => addMenu.removeItem(object)
                }
            }
            hint: "Pick a controller lane to add; dotted ones hold data in this clip"
        }
    }

    Loader {
        x: laneItem.roll.headerWidth
        y: Theme.space1
        width: laneItem.width - x
        height: laneItem.height - 2 * Theme.space1
        clip: true
        sourceComponent: laneItem.lane === "velocity" ? velocityBars : controlValues
    }

    Component {
        id: velocityBars
        VelocityLane {
            roll: laneItem.roll
        }
    }

    Component {
        id: controlValues
        ControlLane {
            roll: laneItem.roll
            choice: laneItem.choice
        }
    }
}
