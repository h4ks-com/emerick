import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

// The Scripts menu as a picker, which script.run opens from the palette.
Popup {
    id: panel
    anchors.centerIn: parent
    width: Theme.paletteWidth
    padding: Theme.space4
    modal: true
    focus: true
    visible: Nav.scriptsOpen
    onClosed: Nav.scriptsOpen = false
    background: GlassPanel {}
    contentItem: Column {
        objectName: "scripts"
        spacing: Theme.space2
        Label {
            text: "Run script"
            color: Theme.ink
            font.pixelSize: Theme.textTitle
        }
        Repeater {
            model: (Registry.menus["Scripts"] || []).filter(entry => entry.row === App.Command)
            delegate: ChromeButton {
                required property var modelData
                objectName: modelData.id
                width: parent.width
                onClicked: panel.close()
            }
        }
    }
}
