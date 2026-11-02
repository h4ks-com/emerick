import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui

Rectangle {
    color: Theme.graphite

    ActionShortcuts {}

    GlassPanel {
        id: controlBar
        width: parent.width
        height: Theme.headerHeight
        radius: 0
        border.width: 0
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            text: Project.name
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
            elide: Text.ElideRight
            width: Math.min(implicitWidth, Theme.labelWidth)
        }
        Row {
            anchors.centerIn: parent
            spacing: Theme.space3
            TransportBar {
                anchors.verticalCenter: parent.verticalCenter
            }
            TransportDisplay {
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    TrackList {
        anchors.top: controlBar.bottom
        anchors.bottom: parent.bottom
        width: parent.width
    }
}
