import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Rectangle {
    id: area
    objectName: "tracks"
    color: Theme.graphite
    clip: true

    // The view scrolls in ticks, so zooming keeps the left edge on the same beat.
    property real scrollTicks: 0
    property int dropIndex: -1
    readonly property real pixelsPerTick: Nav.pixelsPerTick
    readonly property real laneWidth: Math.max(0, width - Theme.trackHeaderWidth)
    readonly property real visibleTicks: laneWidth / pixelsPerTick
    readonly property real totalTicks: Math.max(Project.endTicks + Theme.timelineTailBars * Project.ticksPerBar,
                                                scrollTicks + visibleTicks)
    readonly property int barStep: {
        let step = 1
        while (step * Project.ticksPerBar * pixelsPerTick < Theme.labelSpacing) {
            step *= 2
        }
        return step
    }
    readonly property bool showBeats: barStep === 1 && Project.ticksPerBeat * pixelsPerTick >= Theme.beatSpacing

    function xOf(ticks) {
        return Theme.trackHeaderWidth + (ticks - scrollTicks) * pixelsPerTick
    }

    function scrollTo(ticks) {
        scrollTicks = Math.max(0, ticks)
    }

    function zoomAround(factor, sceneX) {
        const anchor = scrollTicks + (sceneX - Theme.trackHeaderWidth) / pixelsPerTick
        Nav.pixelsPerTick = pixelsPerTick * factor
        scrollTo(anchor - (sceneX - Theme.trackHeaderWidth) / Nav.pixelsPerTick)
    }

    function scrollRows(dy) {
        list.contentY = Math.max(0, Math.min(list.contentY + dy, list.contentHeight - list.height))
    }

    Connections {
        target: Project
        function onPositionTicksChanged() {
            const x = (Project.positionTicks - area.scrollTicks) * area.pixelsPerTick
            if (Project.playing && (x < 0 || x > area.laneWidth * Theme.followEdge)) {
                area.scrollTo(Project.positionTicks - area.visibleTicks * Theme.followLead)
            }
        }
    }

    WheelHandler {
        target: null
        onWheel: event => {
            const dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x * Theme.wheelStep
            const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y * Theme.wheelStep
            if (event.modifiers & Qt.ControlModifier) {
                area.zoomAround(Math.exp(event.angleDelta.y * Theme.wheelZoom), event.x)
            } else if (event.modifiers & Qt.ShiftModifier) {
                area.scrollTo(area.scrollTicks - (dy + dx) / area.pixelsPerTick)
            } else {
                area.scrollTo(area.scrollTicks - dx / area.pixelsPerTick)
                area.scrollRows(-dy)
            }
        }
    }

    PinchHandler {
        id: pinch
        target: null
        property real startZoom: 1
        onActiveChanged: startZoom = Nav.pixelsPerTick
        onActiveScaleChanged: area.zoomAround(startZoom * activeScale / Nav.pixelsPerTick, centroid.position.x)
    }

    Repeater {
        objectName: "grid"
        model: Math.ceil(area.visibleTicks / (area.barStep * Project.ticksPerBar)) + 1
        delegate: Item {
            required property int index
            readonly property int bar: (Math.floor(area.scrollTicks / (area.barStep * Project.ticksPerBar)) + index) * area.barStep
            x: area.xOf(bar * Project.ticksPerBar)
            y: Theme.rulerHeight
            width: area.barStep * Project.ticksPerBar * area.pixelsPerTick
            height: area.height - Theme.rulerHeight
            Rectangle {
                width: Theme.hairline
                height: parent.height
                color: Theme.barLine
            }
            Repeater {
                model: area.showBeats ? Project.ticksPerBar / Project.ticksPerBeat - 1 : 0
                delegate: Rectangle {
                    required property int index
                    x: (index + 1) * Project.ticksPerBeat * area.pixelsPerTick
                    width: Theme.hairline
                    height: parent.height
                    color: Theme.beatLine
                }
            }
        }
    }

    ListView {
        id: list
        objectName: "tracks.list"
        anchors.fill: parent
        anchors.topMargin: Theme.rulerHeight
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: Nav.trackHeight * 4
        model: Tracks
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: row
            required property int index
            required property string trackId
            required property string name
            required property string icon
            required property int colorIndex
            required property bool mute
            required property bool solo
            required property bool armed
            required property bool monitor
            required property real gainDb
            required property var clips
            required property var trackData
            required property string laneComponent
            readonly property color trackColor: Theme.trackColors[colorIndex % Project.trackColorCount]
            width: ListView.view.width
            height: Nav.trackHeight

            Item {
                objectName: "lane:" + row.trackId
                x: Theme.trackHeaderWidth
                width: area.laneWidth
                height: parent.height
                clip: true

                MouseArea {
                    objectName: "selection.clear:" + row.trackId
                    anchors.fill: parent
                    onClicked: {
                        Registry.runAction("selection.clear", {})
                        Registry.runAction("track.select", {track: row.trackId})
                    }
                }

                Item {
                    x: -area.scrollTicks * area.pixelsPerTick
                    width: area.totalTicks * area.pixelsPerTick
                    height: parent.height
                    Loader {
                        anchors.fill: parent
                        active: row.laneComponent !== ""
                        source: row.laneComponent
                        onLoaded: {
                            item.trackId = Qt.binding(() => row.trackId)
                            item.trackData = Qt.binding(() => row.trackData)
                            item.pixelsPerTick = Qt.binding(() => area.pixelsPerTick)
                        }
                    }
                    Repeater {
                        model: row.laneComponent === "" ? row.clips : []
                        delegate: ClipItem {
                            required property var modelData
                            clip: modelData
                            trackName: row.name
                            trackColor: row.trackColor
                            muted: row.mute
                            pixelsPerTick: area.pixelsPerTick
                            viewFrom: area.scrollTicks
                            viewTo: area.scrollTicks + area.visibleTicks
                            height: row.height
                        }
                    }
                }
            }

            TrackHeader {
                width: Theme.trackHeaderWidth
                height: parent.height
                index: row.index
                trackId: row.trackId
                name: row.name
                icon: row.icon
                trackColor: row.trackColor
                mute: row.mute
                solo: row.solo
                armed: row.armed
                monitor: row.monitor
                gainDb: row.gainDb
                onDropIndexChanged: area.dropIndex = dropIndex
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: Theme.rule
            }
        }
    }

    Rectangle {
        objectName: "tracks.dropIndicator"
        visible: area.dropIndex >= 0
        y: Theme.rulerHeight + area.dropIndex * Nav.trackHeight - list.contentY - height / 2
        width: area.width
        height: Theme.selectionWidth
        color: Theme.ink
    }

    Rectangle {
        x: 0
        width: Theme.trackHeaderWidth
        height: Theme.rulerHeight
        color: Theme.slate
        ChromeButton {
            objectName: "edit.toggleSnap"
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2
            implicitHeight: Theme.headerButton
            text: "Snap"
            active: Registry.settings["edit.snap"] === true
            activeColor: Theme.inkDim
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
    }

    Ruler {
        x: Theme.trackHeaderWidth
        width: area.laneWidth
        height: Theme.rulerHeight
        scrollTicks: area.scrollTicks
        pixelsPerTick: area.pixelsPerTick
        barStep: area.barStep
        showBeats: area.showBeats
    }

    Rectangle {
        x: Theme.trackHeaderWidth - Theme.hairline
        width: Theme.hairline
        height: area.height
        color: Theme.rule
    }

    Item {
        objectName: "playhead"
        readonly property real at: area.xOf(Project.positionTicks)
        x: at - width / 2
        width: Theme.playheadCap
        height: area.height
        visible: list.count > 0 && at >= Theme.trackHeaderWidth && at <= area.width
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.hairline
            height: parent.height
            color: Theme.tape
        }
        Canvas {
            width: parent.width
            height: Theme.playheadCap / 2 + Theme.space1
            onPaint: {
                const context = getContext("2d")
                context.fillStyle = Theme.tape
                context.beginPath()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fill()
            }
        }
    }

    ScrollBar {
        id: horizontal
        objectName: "tracks.scroll"
        orientation: Qt.Horizontal
        x: Theme.trackHeaderWidth
        width: area.laneWidth
        anchors.bottom: parent.bottom
        size: area.visibleTicks / area.totalTicks
        visible: list.count > 0
        onPositionChanged: if (pressed) area.scrollTo(position * area.totalTicks)
    }
    Binding {
        target: horizontal
        property: "position"
        value: area.scrollTicks / area.totalTicks
        when: !horizontal.pressed
        restoreMode: Binding.RestoreNone
    }

    Column {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: Theme.space3
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Add a track to start recording"
            color: Theme.inkDim
            font.pixelSize: Theme.textTitle
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space2
            ChromeButton {
                objectName: "track.addAudio"
            }
            ChromeButton {
                objectName: "track.addMidi"
            }
        }
    }
}
