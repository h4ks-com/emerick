import QtQuick
import QtQuick.Controls.Basic
import Emerick.App
import Emerick.Ui

// A timeline lane of named blocks that runs <actionPrefix>.set, .move and .delete on its track.
Item {
    id: lane
    property string trackId
    property real pixelsPerTick: Nav.pixelsPerTick
    property string actionPrefix
    property string nameParam
    property var blocks: []
    property int snapTicks: 960
    property bool editing: false
    property real editAt: 0
    property string editText

    objectName: actionPrefix + ".set:" + trackId

    function snapped(x) {
        return Math.max(0, Math.round(x / pixelsPerTick / snapTicks) * snapTicks)
    }

    function edit(at, text) {
        editAt = at
        editText = text
        editing = true
        editor.forceActiveFocus()
        editor.selectAll()
    }

    function run(action, params) {
        params.track = trackId
        Registry.runAction(actionPrefix + "." + action, params)
    }

    MouseArea {
        anchors.fill: parent
        onClicked: mouse => lane.edit(lane.snapped(mouse.x), "")
    }

    Repeater {
        model: lane.blocks
        delegate: Rectangle {
            id: block
            required property var modelData
            readonly property real homeX: modelData.at * lane.pixelsPerTick
            objectName: lane.actionPrefix + ".move:" + lane.trackId + ":" + modelData.at
            x: homeX
            y: Theme.space1
            width: Math.max(2 * Theme.hairline, (modelData.end - modelData.at) * lane.pixelsPerTick - Theme.hairline)
            height: lane.height - 2 * Theme.space1
            radius: Theme.radiusClip
            color: modelData.gap ? Theme.clear : Theme.trackColors[modelData.colorIndex]
            border.width: activeFocus ? Theme.hairline : 0
            border.color: Theme.tape
            Keys.onDeletePressed: lane.run("delete", {at: modelData.at})
            ToolTip.text: Registry.actionInfo(lane.actionPrefix + ".move").title
            ToolTip.visible: blockArea.containsMouse && !blockArea.pressed
            ToolTip.delay: Theme.tooltipDelay

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.space2
                text: block.modelData.label
                color: block.modelData.gap ? Theme.inkDim : Theme.ink
                font.pixelSize: Theme.textBody
            }

            MouseArea {
                id: blockArea
                anchors.fill: parent
                hoverEnabled: true
                drag.target: block
                drag.axis: Drag.XAxis
                drag.minimumX: 0
                onPressed: block.forceActiveFocus()
                onDoubleClicked: lane.edit(block.modelData.at, block.modelData.label)
                onReleased: {
                    const to = lane.snapped(block.x)
                    block.x = Qt.binding(() => block.homeX)
                    if (to !== block.modelData.at)
                        lane.run("move", {at: block.modelData.at, to: to})
                }
            }
        }
    }

    TextField {
        id: editor
        objectName: lane.actionPrefix + ".set:" + lane.trackId + ":" + lane.editAt
        visible: lane.editing
        x: lane.editAt * lane.pixelsPerTick
        height: lane.height
        width: Theme.labelWidth
        text: lane.editText
        ToolTip.text: Registry.actionInfo(lane.actionPrefix + ".set").title
        ToolTip.visible: hovered && !activeFocus
        ToolTip.delay: Theme.tooltipDelay
        font.pixelSize: Theme.textBody
        onAccepted: {
            const params = {at: lane.editAt}
            params[lane.nameParam] = text
            lane.editing = false
            lane.run("set", params)
        }
        onActiveFocusChanged: if (!activeFocus) lane.editing = false
        // A hidden field keeps the focus it had, which would turn the window's shortcuts off.
        onVisibleChanged: if (!visible) focus = false
        Keys.onEscapePressed: lane.editing = false
    }
}
