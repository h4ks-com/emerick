import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Emerick.App

Rectangle {
    id: inspector
    objectName: "inspector"
    color: Theme.slate

    readonly property DragGroup dragGroup: DragGroup {
        target: inspector
    }

    component RouteLabel: Label {
        color: Theme.inkDim
        font.pixelSize: Theme.textLabel
        Layout.topMargin: Theme.space2
    }

    component AudioRoute: AudioRoutePicker {
        required property string actionId
        required property string trackId
        name: actionId + ":" + trackId
        toolTip: Registry.actionInfo(actionId).title
        onPicked: params => Registry.runAction(actionId, Object.assign({track: trackId}, params))
        Layout.fillWidth: true
    }

    component MidiRoute: RowLayout {
        id: midiRoute
        required property string actionId
        required property string trackId
        required property var ports
        required property var route
        Layout.fillWidth: true
        RouteCombo {
            actionId: midiRoute.actionId
            trackId: midiRoute.trackId
            model: midiRoute.ports
            currentId: midiRoute.route.port
            Layout.fillWidth: true
        }
        RouteCombo {
            actionId: midiRoute.actionId
            trackId: midiRoute.trackId
            part: "channel"
            model: Project.routeDevices.midiChannels
            currentId: String(midiRoute.route.channel)
            Layout.preferredWidth: Theme.channelPickerWidth
        }
    }

    // A clip number that, with several clips selected, applies to every one of them as one undo step.
    component ClipNumber: DragNumber {
        required property var clipPanel
        objectName: actionId + ":" + (clipPanel.clip ? clipPanel.clip.id : "") + ":" + param
        badge: Nav.selectedClips.length > 1 ? Nav.selectedClips.length + " clips" : ""
        actionsFor: number => clipPanel.clipCalls(actionId, {[param]: number})
        Layout.fillWidth: true
    }

    // The shape choices of a fade, which apply to every selected clip. Fade out icons mirror the fade in ones.
    component FadeShapes: RowLayout {
        id: shapes
        required property string actionId
        required property string current
        property bool mirrored: false
        spacing: 0
        Repeater {
            model: Registry.actionInfo(shapes.actionId).choices
            ChromeButton {
                required property var modelData
                objectName: shapes.actionId + ":" + modelData.id
                actionId: shapes.actionId
                params: modelData.params
                iconName: "fade-" + modelData.id
                hint: modelData.title
                active: shapes.current === modelData.id
                transform: Scale {
                    origin.x: Theme.buttonHeight / 2
                    xScale: shapes.mirrored ? -1 : 1
                }
            }
        }
    }

    component PositionNumber: ClipNumber {
        property bool duration: false
        step: Project.ticksPerBeat / 4
        from: 0
        textFromValue: ticks => Project.ticksText(Math.round(ticks), duration)
        valueFromText: text => {
            const ticks = Project.parseTicks(text, duration)
            return ticks < 0 ? NaN : ticks
        }
    }

    Flickable {
        objectName: "inspector.scroll"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: markers.top
        anchors.bottomMargin: Theme.space3
        contentHeight: contentItem.childrenRect.height + 2 * Theme.space3
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Repeater {
            model: Tracks
            delegate: ColumnLayout {
                id: panel
                required property string trackId
                required property string name
                required property var clips
                required property bool audioRoutes
                required property bool midiRoutes
                required property var audioIn
                required property var audioOut
                required property var midiIn
                required property var midiOut
                required property real pan
                readonly property var clip: visible && Nav.selectedClips.length > 0
                                            ? clips.find(entry => entry.id === Nav.selectedClips[0]) : undefined
                readonly property bool audioClip: clip !== undefined && clip.type === "audio"
                readonly property bool severalClips: Nav.selectedClips.length > 1

                function clipCalls(actionId, params) {
                    return Nav.selectedClips.map(id => ({id: actionId, params: Object.assign({clip: id}, params)}))
                }
                visible: trackId === Nav.selectedTrack
                x: Theme.space3
                y: Theme.space3
                width: parent.width - 2 * Theme.space3
                spacing: Theme.space1

                Label {
                    text: panel.name
                    color: Theme.ink
                    font.pixelSize: Theme.textBody
                    font.weight: Font.Medium
                }

                RouteLabel { text: "Pan" }
                PanNumber {
                    trackId: panel.trackId
                    pan: panel.pan
                }

                RouteLabel { text: "Audio input"; visible: panel.audioRoutes }
                AudioRoute {
                    visible: panel.audioRoutes
                    actionId: "track.setAudioIn"
                    trackId: panel.trackId
                    devices: Project.routeDevices.audioIn
                    route: panel.audioIn
                }
                RouteLabel { text: "Audio output"; visible: panel.audioRoutes }
                AudioRoute {
                    visible: panel.audioRoutes
                    actionId: "track.setAudioOut"
                    trackId: panel.trackId
                    devices: Project.routeDevices.audioOut
                    route: panel.audioOut
                }

                RouteLabel { text: "MIDI input"; visible: panel.midiRoutes }
                MidiRoute {
                    visible: panel.midiRoutes
                    actionId: "track.setMidiIn"
                    trackId: panel.trackId
                    ports: Project.routeDevices.midiIn
                    route: panel.midiIn
                }
                RouteLabel { text: "MIDI output"; visible: panel.midiRoutes }
                MidiRoute {
                    visible: panel.midiRoutes
                    actionId: "track.setMidiOut"
                    trackId: panel.trackId
                    ports: Project.routeDevices.midiOut
                    route: panel.midiOut
                }
                Label {
                    objectName: "inspector.trackErrors"
                    visible: text !== ""
                    text: Project.trackErrors[panel.trackId] ?? ""
                    color: Theme.rec
                    font.pixelSize: Theme.textLabel
                    wrapMode: Text.Wrap
                    Layout.topMargin: Theme.space2
                    Layout.fillWidth: true
                }

                Loader {
                    active: panel.clip !== undefined
                    Layout.topMargin: Theme.space4
                    Layout.fillWidth: true
                    sourceComponent: clipProperties
                }

                Component {
                    id: clipProperties
                    GridLayout {
                    columns: 2
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space1

                    Label {
                        text: panel.severalClips ? Nav.selectedClips.length + " clips" : panel.clip ? "Clip, take " + panel.clip.take : ""
                        color: Theme.ink
                        font.pixelSize: Theme.textBody
                        font.weight: Font.Medium
                        Layout.columnSpan: 2
                    }
                    RouteLabel { text: "Start" }
                    PositionNumber {
                        clipPanel: panel
                        actionId: "clip.move"
                        param: "start"
                        value: panel.clip ? panel.clip.start : 0
                        // clip.move moves the whole selection by the same offset itself.
                        actionsFor: number => [{id: "clip.move", params: {clip: panel.clip.id, start: number, selection: true}}]
                    }
                    RouteLabel { text: "End"; visible: !panel.severalClips }
                    PositionNumber {
                        visible: !panel.severalClips
                        clipPanel: panel
                        actionId: "clip.resize"
                        param: "end"
                        from: panel.clip ? panel.clip.start + 1 : 0
                        value: panel.clip ? panel.clip.start + panel.clip.length : 0
                    }
                    RouteLabel { text: "Length" }
                    PositionNumber {
                        clipPanel: panel
                        actionId: "clip.resize"
                        param: "length"
                        duration: true
                        from: 1
                        value: panel.clip ? panel.clip.length : 0
                    }
                    RouteLabel {
                        text: "Fades"
                        visible: panel.audioClip
                        Layout.columnSpan: 2
                    }
                    RouteLabel { text: "In"; visible: panel.audioClip }
                    ClipNumber {
                        visible: panel.audioClip
                        clipPanel: panel
                        actionId: "clip.setFade"
                        param: "fadeIn"
                        from: 0
                        to: panel.clip ? panel.clip.length : 0
                        step: Project.ticksPerBeat / 16
                        unit: "ticks"
                        value: panel.audioClip ? panel.clip.fadeIn : 0
                    }
                    RouteLabel { text: "In shape"; visible: panel.audioClip }
                    FadeShapes {
                        visible: panel.audioClip
                        actionId: "clip.setFadeInShape"
                        current: panel.audioClip ? panel.clip.fadeInShape : ""
                    }
                    RouteLabel { text: "In curve"; visible: panel.audioClip }
                    ClipNumber {
                        visible: panel.audioClip
                        clipPanel: panel
                        actionId: "clip.setFade"
                        param: "fadeInCurve"
                        from: -1
                        to: 1
                        step: 0.05
                        decimals: 2
                        value: panel.audioClip ? panel.clip.fadeInCurve : 0
                    }
                    RouteLabel { text: "Out"; visible: panel.audioClip }
                    ClipNumber {
                        visible: panel.audioClip
                        clipPanel: panel
                        actionId: "clip.setFade"
                        param: "fadeOut"
                        from: 0
                        to: panel.clip ? panel.clip.length : 0
                        step: Project.ticksPerBeat / 16
                        unit: "ticks"
                        value: panel.audioClip ? panel.clip.fadeOut : 0
                    }
                    RouteLabel { text: "Out shape"; visible: panel.audioClip }
                    FadeShapes {
                        visible: panel.audioClip
                        actionId: "clip.setFadeOutShape"
                        current: panel.audioClip ? panel.clip.fadeOutShape : ""
                        mirrored: true
                    }
                    RouteLabel { text: "Out curve"; visible: panel.audioClip }
                    ClipNumber {
                        visible: panel.audioClip
                        clipPanel: panel
                        actionId: "clip.setFade"
                        param: "fadeOutCurve"
                        from: -1
                        to: 1
                        step: 0.05
                        decimals: 2
                        value: panel.audioClip ? panel.clip.fadeOutCurve : 0
                    }
                    RouteLabel { text: "Gain"; visible: panel.audioClip }
                    ClipNumber {
                        visible: panel.audioClip
                        clipPanel: panel
                        actionId: "clip.setGain"
                        param: "db"
                        from: Theme.gainMinDb
                        to: Theme.clipGainMaxDb
                        step: Theme.gainStepDb
                        decimals: 1
                        unit: "dB"
                        value: panel.audioClip ? panel.clip.gainDb : 0
                    }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: markers
        objectName: "inspector.markers"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.space3
        spacing: Theme.space1

        Label {
            text: "Markers"
            color: Theme.ink
            font.pixelSize: Theme.textBody
            font.weight: Font.Medium
        }
        Label {
            visible: Project.markers.length === 0
            text: "No markers yet, " + Theme.shortcutText(Registry.actionInfo("marker.add").shortcut) + " to add one at the playhead"
            color: Theme.inkDim
            font.pixelSize: Theme.textLabel
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ListView {
            clip: true
            model: Project.markers
            boundsBehavior: Flickable.StopAtBounds
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count, Theme.markerListRows) * Theme.buttonHeight
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                objectName: "marker.goto:" + modelData.id
                width: ListView.view.width
                height: Theme.buttonHeight
                highlighted: Nav.selectedMarker === modelData.id
                ToolTip.text: Registry.actionInfo("marker.goto").title + " " + modelData.number
                ToolTip.visible: hovered
                ToolTip.delay: Theme.tooltipDelay
                onClicked: Registry.runAction("marker.goto", {marker: modelData.id})
                background: Rectangle {
                    radius: Theme.radiusClip
                    color: row.highlighted ? Theme.glow(Theme.tape) : row.hovered ? Theme.lift : Theme.clear
                }
                contentItem: RowLayout {
                    spacing: Theme.space2
                    Label {
                        text: row.modelData.number
                        color: Theme.inkDim
                        font.pixelSize: Theme.textLabel
                        font.features: {"tnum": 1}
                    }
                    Label {
                        text: row.modelData.name
                        color: Theme.ink
                        font.pixelSize: Theme.textLabel
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.modelData.position
                        color: Theme.inkDim
                        font.pixelSize: Theme.textLabel
                        font.features: {"tnum": 1}
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: Nav.selectedTrack === ""
        text: "Select a track to see its routes"
        color: Theme.inkDim
        font.pixelSize: Theme.textLabel
    }
}
