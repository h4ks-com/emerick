import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

Popup {
    id: panel
    anchors.centerIn: parent
    width: Math.min(Theme.paletteWidth, parent.width - 2 * Theme.space4)
    padding: Theme.space4
    modal: true
    focus: true
    visible: true
    onClosed: {
        Nav.librariesOpen = false
        Nav.shownLibrary = ""
    }
    onOpened: {
        const index = Libraries.list.findIndex(library => library.id === Nav.shownLibrary)
        if (index >= 0)
            list.positionViewAtIndex(index, ListView.Contain)
    }
    background: GlassPanel {}
    contentItem: Column {
        objectName: "library.list"
        spacing: Theme.space2
        Label {
            text: "Sound libraries"
            color: Theme.ink
            font.pixelSize: Theme.textTitle
        }
        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight, Theme.paletteListHeight)
            clip: true
            spacing: Theme.space1
            model: Libraries.list
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool busy: modelData.state === "downloading" || modelData.state === "extracting"
                property bool confirming: false
                objectName: "library.row:" + modelData.id
                width: ListView.view.width
                implicitHeight: rowContent.implicitHeight + 2 * Theme.space2
                radius: Theme.radiusClip
                color: modelData.id === Nav.shownLibrary ? Theme.lift : Theme.clear
                ColumnLayout {
                    id: rowContent
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    spacing: Theme.space1
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            color: Theme.ink
                            font.pixelSize: Theme.textBody
                            elide: Text.ElideRight
                        }
                        Label {
                            text: Math.round(row.modelData.size / 1e6) + " MB · " + row.modelData.license
                            color: Theme.inkDim
                            font.pixelSize: Theme.textSmall
                            font.features: {"tnum": 1}
                        }
                        ChromeButton {
                            objectName: "library.install:" + row.modelData.id
                            actionId: "library.install"
                            params: ({library: row.modelData.id})
                            text: "Install"
                            visible: row.modelData.state === "available"
                        }
                        ChromeButton {
                            objectName: "library.cancel:" + row.modelData.id
                            actionId: "library.cancel"
                            params: ({library: row.modelData.id})
                            text: "Cancel download"
                            visible: row.busy
                        }
                        ChromeButton {
                            objectName: "library.remove:" + row.modelData.id
                            actionId: "library.remove"
                            text: row.confirming ? "Confirm remove" : "Remove"
                            active: row.confirming
                            activeColor: Theme.rec
                            visible: row.modelData.installed && !row.busy
                            run: () => {
                                if (row.confirming)
                                    Registry.runAction("library.remove", {library: row.modelData.id, confirm: true})
                                row.confirming = !row.confirming
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.description
                        color: Theme.inkDim
                        font.pixelSize: Theme.textLabel
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        objectName: "library.progress:" + row.modelData.id
                        Layout.fillWidth: true
                        implicitHeight: Theme.progressHeight
                        visible: row.busy
                        radius: height / 2
                        color: Theme.rule
                        Rectangle {
                            width: row.modelData.state === "extracting"
                                   ? parent.width : parent.width * Math.min(1, row.modelData.received / row.modelData.size)
                            height: parent.height
                            radius: parent.radius
                            color: Theme.tape
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: row.modelData.state === "extracting" ? "Extracting"
                              : row.modelData.state === "installed" ? row.modelData.path
                              : row.modelData.error
                        color: row.modelData.state === "available" ? Theme.rec : Theme.inkDim
                        font.pixelSize: Theme.textSmall
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
