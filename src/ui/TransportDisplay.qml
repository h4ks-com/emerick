import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Rectangle {
    implicitWidth: fields.implicitWidth + 2 * Theme.space3
    implicitHeight: Theme.displayHeight
    color: Theme.graphite
    radius: Theme.radiusClip
    border.color: Theme.rule

    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: Theme.hairline
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 2 * parent.radius
        height: Theme.hairline
        color: Theme.displayInset
    }

    component Field: Column {
        id: field
        property string value
        property string label
        property string widest: "0"
        default property alias editor: slot.data
        spacing: -Theme.space1
        TextMetrics {
            id: metrics
            font: digits.font
            text: field.widest
        }
        Item {
            id: slot
            width: Math.max(metrics.advanceWidth, digits.implicitWidth)
            height: digits.implicitHeight
            Label {
                id: digits
                anchors.horizontalCenter: parent.horizontalCenter
                text: field.value
                color: Theme.tape
                font.pixelSize: Theme.textCounter
                font.weight: Font.Light
                font.features: {"tnum": 1}
            }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: field.label
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
        }
    }

    component Divider: Rectangle {
        width: Theme.hairline
        height: Theme.displayHeight - 2 * Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.rule
    }

    Row {
        id: fields
        objectName: "transport.position"
        anchors.centerIn: parent
        spacing: Theme.space3
        Field {
            objectName: "transport.position.bar"
            value: Clock.bar
            label: "bar"
            widest: "000"
        }
        Field {
            objectName: "transport.position.beat"
            value: Clock.beat
            label: "beat"
        }
        Field {
            objectName: "transport.position.division"
            value: Clock.division
            label: "div"
        }
        Field {
            objectName: "transport.position.tick"
            value: Clock.tick
            label: "tick"
            widest: "000"
        }
        Divider {}
        Field {
            id: tempo
            objectName: "project.setTempo"
            label: "tempo"
            widest: "000"
            DragNumber {
                objectName: "project.setTempo.bpm"
                anchors.fill: parent
                actionId: "project.setTempo"
                framed: false
                value: Number(Clock.bpm)
                from: Theme.minBpm
                to: Theme.maxBpm
                decimals: 1
                textFromValue: bpm => String(Math.round(bpm * 10) / 10)
                textColor: Theme.tape
                font.pixelSize: Theme.textCounter
                font.weight: Font.Light
                onCommitted: bpm => Registry.runAction("project.setTempo", {bpm: bpm})
            }
        }
        Field {
            value: Clock.meter
            label: "time"
        }
    }
}
