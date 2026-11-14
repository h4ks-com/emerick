import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

// A track's channel strip preset, after Logic's Setting button: it names the preset the track loaded or saved last,
// and its menu saves the track's inserts as a preset or loads one.
ColumnLayout {
    id: presets
    required property string trackId
    required property string preset
    property var choices: []
    spacing: Theme.space1

    ChromeButton {
        objectName: "track.loadPreset:" + presets.trackId
        actionId: "track.loadPreset"
        text: presets.preset !== "" ? presets.preset : info.title
        hint: "Save this track's inserts as a preset, or load one"
        textSize: Theme.textSmall
        padding: Theme.space1
        implicitHeight: Theme.headerButton
        menu: presetMenu
        Layout.fillWidth: true

        GlassMenu {
            id: presetMenu
            objectName: "track.loadPreset:" + presets.trackId + ":menu"
            marks: true
            onAboutToShow: presets.choices = Registry.actionInfo("track.loadPreset").choices ?? []

            GlassMenuItem {
                objectName: "track.savePreset:" + presets.trackId
                text: Registry.actionInfo("track.savePreset").title ?? ""
                onTriggered: nameField.visible = true
            }
            Instantiator {
                model: presets.choices
                delegate: ActionMenuItem {
                    required property var modelData
                    objectName: "track.loadPreset:" + presets.trackId + ":" + modelData.id
                    actionId: "track.loadPreset"
                    title: modelData.title
                    marked: modelData.id === presets.preset
                    params: Object.assign({track: presets.trackId}, modelData.params)
                }
                onObjectAdded: (index, object) => presetMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => presetMenu.removeItem(object)
            }
        }
    }

    TextField {
        id: nameField
        objectName: "track.savePreset:" + presets.trackId + ":name"
        visible: false
        placeholderText: "Name the preset, Return to save"
        placeholderTextColor: Theme.inkDim
        ToolTip.text: Registry.actionInfo("track.savePreset").title ?? ""
        ToolTip.visible: hovered && !activeFocus
        ToolTip.delay: Theme.tooltipDelay
        padding: 0
        leftPadding: Theme.space1
        implicitHeight: Theme.headerButton
        color: Theme.ink
        font.pixelSize: Theme.textSmall
        Layout.fillWidth: true
        background: Rectangle {
            color: Theme.graphite
            radius: Theme.radiusClip
            border.color: Theme.rule
        }
        // A hidden field keeps the focus it had, which would turn the window's shortcuts off.
        onVisibleChanged: {
            if (visible) {
                text = presets.preset
                forceActiveFocus()
                selectAll()
            } else {
                focus = false
            }
        }
        onAccepted: {
            if (text.length > 0) {
                Registry.runAction("track.savePreset", {track: presets.trackId, name: text})
            }
            visible = false
        }
        Keys.onEscapePressed: visible = false
        onActiveFocusChanged: if (!activeFocus) visible = false
    }
}
