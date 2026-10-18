import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import Emerick.App

// The one picker every view uses: a raised field and a glass popup.
ComboBox {
    id: control
    implicitHeight: Theme.buttonHeight
    leftPadding: Theme.space2
    rightPadding: Theme.space2 + Theme.iconSize
    font.pixelSize: Theme.textLabel

    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.ink : Theme.inkDim
        font: control.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: ColorImage {
        x: control.width - width - Theme.space2
        y: (control.height - height) / 2
        color: Theme.inkDim
        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Basic/images/double-arrow.png"
    }
    background: Rectangle {
        implicitWidth: Theme.labelWidth
        radius: Theme.radiusClip
        color: control.down || control.hovered ? Theme.rule : Theme.lift
    }
    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        width: ListView.view.width
        implicitHeight: Theme.buttonHeight
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole ? row.modelData[control.textRole] : row.modelData
            color: Theme.ink
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusClip
            color: row.highlighted ? Theme.lift : Theme.clear
        }
    }
    popup: Popup {
        y: control.height + Theme.space1
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, control.Window.height - y)
        padding: Theme.space1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
        }
        background: GlassPanel {}
    }
}
