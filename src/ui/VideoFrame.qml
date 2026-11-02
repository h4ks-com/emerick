import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Rectangle {
    color: Theme.graphite

    GlassPanel {
        id: header
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
            anchors.leftMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            text: Project.name
            color: Theme.ink
            font.pixelSize: Theme.textTitle
            elide: Text.ElideRight
            width: Math.min(implicitWidth, Theme.labelWidth)
        }
        TransportDisplay {
            anchors.centerIn: parent
        }
    }

    TrackList {
        objectName: "video.tracks"
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
    }
}
