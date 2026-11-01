import QtQuick
import Emerick.App

// The controls of an effect without a view of its own: a knob per number, a button per switch and a menu per choice.
// A plugin insert lists its plugin's params.
Flow {
    id: panel
    required property var insert
    readonly property var params: insert.pluginParams ?? Effects.params(insert.type)
    spacing: Theme.space3
    width: Math.ceil(params.length / 2) * (Theme.effectKnobWidth + spacing) - spacing

    Repeater {
        model: panel.params
        delegate: Loader {
            id: slot
            required property var modelData
            sourceComponent: modelData.kind === "toggle" ? toggle : modelData.kind === "choice" ? choice : knob

            Component {
                id: knob
                EffectKnob {
                    insertId: panel.insert.id
                    param: slot.modelData
                    paramValue: panel.insert.params[slot.modelData.id] ?? slot.modelData.defaultValue
                }
            }
            Component {
                id: toggle
                Item {
                    width: Theme.effectKnobWidth
                    height: Theme.effectKnobHeight
                    ParamToggle {
                        anchors.centerIn: parent
                        insertId: panel.insert.id
                        param: slot.modelData
                        paramValue: panel.insert.params[slot.modelData.id] ?? slot.modelData.defaultValue
                    }
                }
            }
            Component {
                id: choice
                Column {
                    width: Theme.effectKnobWidth
                    height: Theme.effectKnobHeight
                    spacing: Theme.space1
                    Text {
                        width: parent.width
                        text: slot.modelData.title
                        color: Theme.inkDim
                        font.pixelSize: Theme.textSmall
                        horizontalAlignment: Text.AlignHCenter
                    }
                    ParamChoice {
                        anchors.horizontalCenter: parent.horizontalCenter
                        insertId: panel.insert.id
                        param: slot.modelData
                        paramValue: panel.insert.params[slot.modelData.id] ?? slot.modelData.defaultValue
                    }
                }
            }
        }
    }
}
