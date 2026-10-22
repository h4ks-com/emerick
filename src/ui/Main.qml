import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Emerick.App

ApplicationWindow {
    id: window
    objectName: "window.main"
    width: Theme.windowWidth
    height: Theme.windowHeight
    visible: true
    title: Project.name + (Project.edited ? "  •" : "")
    color: Theme.graphite

    // We set the palette for the stock controls and for the dialogs Qt draws itself on Linux.
    palette.window: Theme.slate
    palette.windowText: Theme.ink
    palette.base: Theme.lift
    palette.alternateBase: Theme.slate
    palette.text: Theme.ink
    palette.placeholderText: Theme.inkDim
    palette.button: Theme.lift
    palette.buttonText: Theme.ink
    palette.brightText: Theme.ink
    palette.highlight: Theme.rule
    palette.highlightedText: Theme.ink
    palette.light: Theme.lift
    palette.midlight: Theme.rule
    palette.mid: Theme.rule
    palette.dark: Theme.inkDim
    palette.shadow: Theme.graphite
    palette.toolTipBase: Theme.slate
    palette.toolTipText: Theme.ink

    // macOS shows these menus in its global menu bar; elsewhere they sit at the top of the window.
    menuBar: MenuBar {
        id: menus
        background: Rectangle {
            color: Theme.slate
        }
        Instantiator {
            model: Registry.menuBar
            delegate: ActionMenu {
                required property string modelData
                objectName: "menu." + modelData.toLowerCase()
                title: modelData
                menuName: modelData
            }
            onObjectAdded: (index, object) => menus.insertMenu(index, object)
            onObjectRemoved: (index, object) => menus.removeMenu(object)
        }
    }

    FileDialog {
        id: fileDialog
        property string actionId
        onAccepted: Registry.runWithFile(actionId, selectedFile)
    }
    FolderDialog {
        id: folderDialog
        property string actionId
        onAccepted: Registry.runWithFile(actionId, selectedFolder)
    }
    MessageDialog {
        id: confirmDialog
        property string actionId
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: Registry.runAction(actionId, {confirm: true})
    }
    Connections {
        target: Registry
        function onFileDialogRequested(request) {
            if (request.mode === App.Folder) {
                folderDialog.actionId = request.id
                folderDialog.title = request.title
                folderDialog.open()
                return
            }
            fileDialog.actionId = request.id
            fileDialog.title = request.title
            fileDialog.fileMode = request.mode === App.Save ? FileDialog.SaveFile : FileDialog.OpenFile
            fileDialog.nameFilters = request.nameFilter ? [request.nameFilter] : []
            fileDialog.defaultSuffix = request.suffix
            fileDialog.open()
        }
        function onConfirmRequested(request) {
            confirmDialog.actionId = request.id
            confirmDialog.title = request.title
            confirmDialog.text = request.text
            confirmDialog.open()
        }
    }

    Instantiator {
        model: Registry.shortcuts
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.shortcut
            context: Qt.ApplicationShortcut
            enabled: !(window.activeFocusItem instanceof TextInput)
            onActivated: Registry.activate(modelData.id)
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: Registry.gesturing
        onActivated: Registry.cancelGesture()
    }

    header: GlassPanel {
        height: Theme.headerHeight
        radius: 0
        border.width: 0
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            ChromeButton {
                objectName: "view.toggleInspector"
                iconName: "sidebar-simple"
                active: Nav.inspectorOpen
                activeColor: Theme.ink
            }
            ChromeButton {
                objectName: "view.toggleMixer"
                iconName: "sliders"
                active: Nav.mixerOpen
                activeColor: Theme.ink
            }
            ChromeButton {
                objectName: "view.togglePianoRoll"
                iconName: "music-notes"
                active: Nav.pianoRollOpen
                activeColor: Theme.ink
            }
            ChromeButton {
                id: projectButton
                objectName: "project.showMenu"
                iconName: "file"
                active: Nav.projectMenuOpen
                activeColor: Theme.ink
                ActionMenu {
                    objectName: "menu.project"
                    menuName: "Project"
                    y: projectButton.height + Theme.space1
                    visible: Nav.projectMenuOpen
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    onClosed: Nav.projectMenuOpen = false
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: Theme.space1
                text: window.title
                color: Theme.inkDim
                font.pixelSize: Theme.textLabel
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Theme.labelWidth)
            }
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
            Label {
                objectName: "transport.streamError"
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                text: Project.streamError
                color: Theme.rec
                font.pixelSize: Theme.textLabel
                wrapMode: Text.Wrap
                width: Theme.labelWidth
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            ChromeButton {
                objectName: "track.addAudio"
                iconName: "waveform"
            }
            ChromeButton {
                objectName: "track.addMidi"
                iconName: "piano-keys"
            }
            ChromeButton {
                objectName: "palette.open"
                iconName: "magnifying-glass"
            }
            ChromeButton {
                objectName: "settings.open"
                iconName: "gear-six"
            }
        }
    }

    RowLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: pianoRollDock.top
        visible: Nav.page === App.Main
        spacing: 0
        Inspector {
            visible: Nav.inspectorOpen
            Layout.preferredWidth: Theme.inspectorWidth
            Layout.fillHeight: true
        }
        Rectangle {
            visible: Nav.inspectorOpen
            Layout.preferredWidth: Theme.hairline
            Layout.fillHeight: true
            color: Theme.rule
        }
        TrackList {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // The open docks share at most dockShare of the window: each keeps its minimum and they give up the height above it
    // in proportion, so the tracks always keep the rest.
    readonly property var docks: [pianoRollDock, mixerDock, consoleDock].filter(dock => dock.active)
    readonly property real dockBudget: contentItem.height * Theme.dockShare
    readonly property real dockMinimums: docks.reduce((sum, dock) => sum + dock.minimum, 0)
    readonly property real dockWanted: docks.reduce((sum, dock) => sum + dock.panelHeight, 0)
    readonly property real dockGive: Math.min(1, Math.max(0, dockBudget - dockMinimums) / Math.max(1, dockWanted - dockMinimums))

    // A panel loads only while open, so its height lives here and survives closing it.
    component Dock: Loader {
        property real panelHeight
        property real minimum
        anchors.left: parent.left
        anchors.right: parent.right
        height: !active ? 0 : window.dockMinimums > window.dockBudget ? minimum * window.dockBudget / window.dockMinimums
                                                                        : minimum + (panelHeight - minimum) * window.dockGive
        function resizeBy(delta) {
            panelHeight = Math.max(minimum, Math.min(window.dockBudget, panelHeight + delta))
        }
    }

    Dock {
        id: pianoRollDock
        panelHeight: Theme.pianoRollHeight
        minimum: Theme.pianoRollMinHeight
        anchors.bottom: mixerDock.top
        active: Nav.pianoRollOpen && Nav.page === App.Main
        sourceComponent: PianoRoll {
            onResizeBy: delta => pianoRollDock.resizeBy(delta)
        }
    }

    Dock {
        id: mixerDock
        panelHeight: Theme.mixerHeight
        minimum: Theme.mixerMinHeight
        anchors.bottom: consoleDock.top
        active: Nav.mixerOpen && Nav.page === App.Main
        sourceComponent: Mixer {
            onResizeBy: delta => mixerDock.resizeBy(delta)
        }
    }

    Dock {
        id: consoleDock
        panelHeight: Theme.consoleHeight
        minimum: Theme.consoleMinHeight
        anchors.bottom: parent.bottom
        active: Nav.consoleOpen && Nav.page === App.Main
        sourceComponent: ConsolePanel {
            onResizeBy: delta => consoleDock.resizeBy(delta)
        }
    }

    Loader {
        anchors.fill: parent
        active: Nav.page === App.Settings
        sourceComponent: SettingsPage {}
    }

    CommandPalette {}

    Popup {
        anchors.centerIn: parent
        padding: Theme.space5
        modal: true
        visible: Nav.aboutOpen
        onClosed: Nav.aboutOpen = false
        background: GlassPanel {}
        contentItem: Column {
            objectName: "about"
            spacing: Theme.space2
            Label {
                text: "Emerick " + Qt.application.version
                color: Theme.ink
                font.pixelSize: Theme.textTitle
            }
            Label {
                text: "A small DAW for recording instruments. GPL-3.0-or-later."
                color: Theme.inkDim
                font.pixelSize: Theme.textLabel
            }
        }
    }
}
