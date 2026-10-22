import QtQuick
import QtQuick.Layouts
import Emerick.App

// The Channel EQ: its response curve over a column per band, each with its switch, frequency, gain or slope, and Q.
ColumnLayout {
    id: editor
    required property var insert
    readonly property var params: Effects.params("eq")
    readonly property DragGroup dragGroup: DragGroup {
        target: editor
    }
    spacing: Theme.space2

    function valueOf(index) {
        return insert.params[params[index].id] ?? params[index].defaultValue
    }

    EqGraph {
        insert: editor.insert
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.eqGraphHeight
    }

    Row {
        spacing: Theme.space1
        Repeater {
            model: Theme.eqBands
            Column {
                id: band
                required property int index
                readonly property int first: index * Theme.eqBandParams
                readonly property bool cut: index === 0 || index === Theme.eqBands - 1
                width: Theme.eqBandWidth
                spacing: Theme.space1

                Rectangle {
                    width: parent.width
                    height: Theme.colorStripe
                    radius: height / 2
                    color: Theme.trackColors[band.index % Theme.trackColors.length]
                    opacity: editor.valueOf(band.first) >= 0.5 ? 1 : Theme.mutedOpacity
                }
                ParamToggle {
                    width: parent.width
                    insertId: editor.insert.id
                    param: editor.params[band.first]
                    paramValue: editor.valueOf(band.first)
                }
                ParamField {
                    width: parent.width
                    implicitHeight: Theme.headerButton
                    font.pixelSize: Theme.textSmall
                    insertId: editor.insert.id
                    param: editor.params[band.first + 1]
                    paramValue: editor.valueOf(band.first + 1)
                }
                Loader {
                    width: parent.width
                    sourceComponent: band.cut ? slope : gain
                    Component {
                        id: gain
                        ParamField {
                            implicitHeight: Theme.headerButton
                            font.pixelSize: Theme.textSmall
                            insertId: editor.insert.id
                            param: editor.params[band.first + 2]
                            paramValue: editor.valueOf(band.first + 2)
                        }
                    }
                    Component {
                        id: slope
                        ParamChoice {
                            insertId: editor.insert.id
                            param: editor.params[band.first + 2]
                            paramValue: editor.valueOf(band.first + 2)
                        }
                    }
                }
                ParamField {
                    width: parent.width
                    implicitHeight: Theme.headerButton
                    font.pixelSize: Theme.textSmall
                    insertId: editor.insert.id
                    param: editor.params[band.first + 3]
                    paramValue: editor.valueOf(band.first + 3)
                }
            }
        }
    }
}
