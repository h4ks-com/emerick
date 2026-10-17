import QtQuick

Rectangle {
    color: Theme.glass
    radius: Theme.radiusWindow
    border.color: Theme.rule

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 2 * parent.radius
        height: Theme.hairline
        color: Theme.highlight
    }
}
