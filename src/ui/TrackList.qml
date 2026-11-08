import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Rectangle {
    id: area
    objectName: "tracks"
    color: Theme.graphite
    clip: true

    // The view scrolls in ticks, so zooming keeps the left edge on the same beat.
    readonly property real scrollTicks: Nav.scrollTicks
    property int dropIndex: -1
    // A clip drag across tracks: the row it started on, how many rows it moved and the clip's type.
    property int dragFrom: -1
    property int dragRows: 0
    property string dragType
    // A right click opens the track menu at the pointer; the header button and agents open it at the header.
    property bool menuAtCursor: false
    readonly property DragGroup dragGroup: DragGroup {
        target: area
    }
    readonly property real pixelsPerTick: Nav.pixelsPerTick
    readonly property int headerWidth: Registry.settings["view.headerWidth"]
    readonly property int lanesTop: Theme.rulerHeight + Theme.markerLaneHeight
    readonly property real laneWidth: Math.max(0, width - area.headerWidth)
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
        return area.headerWidth + (ticks - scrollTicks) * pixelsPerTick
    }

    function scrollTo(ticks) {
        Nav.scrollTicks = Math.max(0, ticks)
    }

    function trackIdAt(row) {
        const item = list.itemAtIndex(row)
        return item ? item.trackId : ""
    }

    // We find rows by their position, because tracks with lanes open below them are taller. A y below the last track
    // gives the track count.
    function rowAt(y) {
        const content = y - area.lanesTop + list.contentY
        const index = list.indexAt(0, content)
        return index >= 0 ? index : content < 0 ? 0 : list.count
    }
    // The y of the row's top, or of the last track's bottom for the track count.
    function rowTop(row) {
        const item = list.itemAtIndex(Math.max(0, Math.min(row, list.count - 1)))
        const top = item === null ? row * Nav.trackHeight : row >= list.count ? item.y + item.height : item.y
        return area.lanesTop + top - list.contentY
    }

    function rowAccepts(row, type) {
        const item = list.itemAtIndex(row)
        return item !== null && item.clipType === type
    }

    // Dropped files go one per track from the row under the pointer; a file whose row is missing or takes another kind
    // of clip gets a new track.
    // A track kind can take files of its own, such as a style on a Style track.
    function fileDropAt(row, path) {
        const item = list.itemAtIndex(row)
        const drop = item ? item.fileDrop : undefined
        return drop && drop.extensions.some(extension => path.toLowerCase().endsWith("." + extension)) ? drop : undefined
    }

    function importFiles(urls, firstRow, at) {
        const calls = Array.from(urls).map((url, index) => {
            const path = Registry.localPath(url)
            const midi = /\.midi?$/i.test(path)
            const row = firstRow + index
            const drop = area.fileDropAt(row, path)
            if (drop) {
                return {id: drop.action, params: {track: area.trackIdAt(row), path: path}}
            }
            const params = {path: path, at: at}
            if (row < list.count && area.rowAccepts(row, midi ? "midi" : "audio")) {
                params.track = area.trackIdAt(row)
            } else if (!midi) {
                params.newTrack = true
            }
            return {id: midi ? "midi.import" : "clip.import", params: params}
        })
        if (calls.length === 1) {
            Registry.runAction(calls[0].id, calls[0].params)
        } else {
            Registry.runActions("import files", calls)
        }
    }

    function openClipMenu() {
        clipMenu.popup()
    }

    function openLaneMenu() {
        laneMenu.popup()
    }

    Binding {
        target: Nav
        property: "laneWidth"
        value: area.laneWidth
    }

    ActionMenu {
        id: trackMenu
        objectName: "menu.trackContext"
        menuName: "Track context"
        params: ({track: Nav.trackMenu, selection: true})
        onClosed: Nav.trackMenu = ""
    }
    Connections {
        target: Nav
        function onChanged() {
            if (Nav.page !== App.Main) {
                trackMenu.close()
            }
            if (Nav.trackMenu === "" || trackMenu.visible) {
                return
            }
            if (area.menuAtCursor) {
                area.menuAtCursor = false
                trackMenu.popup()
                return
            }
            const rows = [...Array(list.count).keys()]
            const row = rows.find(index => area.trackIdAt(index) === Nav.trackMenu)
            const top = area.rowTop(row ?? 0)
            trackMenu.popup(area, area.headerWidth - Theme.space5, top + Theme.space5)
        }
    }
    ActionMenu {
        id: clipMenu
        objectName: "menu.clipContext"
        menuName: "Clip context"
        params: ({})
    }
    ActionMenu {
        id: laneMenu
        objectName: "menu.laneContext"
        menuName: "Tracks area context"
    }

    MouseArea {
        objectName: "tracks.empty"
        enabled: !Registry.readOnly
        anchors.fill: parent
        anchors.topMargin: area.lanesTop
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            Registry.runAction("selection.clear", {})
            if (Theme.isContextClick(mouse)) {
                area.openLaneMenu()
            }
        }
    }

    function zoomAround(factor, sceneX) {
        const anchor = scrollTicks + (sceneX - area.headerWidth) / pixelsPerTick
        Nav.pixelsPerTick = pixelsPerTick * factor
        scrollTo(anchor - (sceneX - area.headerWidth) / Nav.pixelsPerTick)
    }

    function scrollRows(dy) {
        list.contentY = Math.max(0, Math.min(list.contentY + dy, list.contentHeight - list.height))
    }

    Connections {
        target: Project
        function onPositionTicksChanged() {
            const x = (Project.positionTicks - area.scrollTicks) * area.pixelsPerTick
            const edge = Project.playing ? area.laneWidth * Theme.followEdge : area.laneWidth
            if (x < 0 || x > edge) {
                area.scrollTo(Project.positionTicks - area.visibleTicks * Theme.followLead)
            }
        }
    }

    // We use a MouseArea because a WheelHandler accepts one orientation and drops a tilt wheel's horizontal events.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: event => {
            const dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x * Theme.wheelStep
            const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y * Theme.wheelStep
            if (event.modifiers & Qt.AltModifier) {
                Nav.trackHeight = Nav.trackHeight + Math.round(dy * Theme.wheelTrackHeight)
                area.zoomAround(Math.exp(event.angleDelta.x * Theme.wheelZoom), event.x)
            } else if (event.modifiers & Qt.ControlModifier) {
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

    Item {
        anchors.fill: parent
        anchors.leftMargin: area.headerWidth
        clip: true
        Repeater {
            objectName: "grid"
            model: Math.ceil(area.visibleTicks / (area.barStep * Project.ticksPerBar)) + 1
            delegate: Item {
                required property int index
                readonly property int bar: (Math.floor(area.scrollTicks / (area.barStep * Project.ticksPerBar)) + index) * area.barStep
                x: area.xOf(bar * Project.ticksPerBar) - area.headerWidth
                y: area.lanesTop
                width: area.barStep * Project.ticksPerBar * area.pixelsPerTick
                height: area.height - area.lanesTop
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
    }

    ListView {
        id: list
        objectName: "tracks.list"
        anchors.fill: parent
        anchors.topMargin: area.lanesTop
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: Nav.trackHeight * 4
        model: Tracks
        ScrollBar.vertical: ScrollBar {}
        footer: Item {
            objectName: "tracks.addRow"
            visible: !Registry.readOnly
            width: ListView.view.width
            height: visible ? Math.round(Nav.trackHeight * Theme.addRowScale) : 0
            Rectangle {
                width: area.headerWidth
                height: parent.height
                color: Theme.slate
                Row {
                    visible: !Registry.readOnly
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.colorStripe + Theme.space2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space1
                    ChromeButton {
                        objectName: "track.addAudio"
                        iconName: "waveform"
                        iconColor: Theme.inkDim
                    }
                    ChromeButton {
                        objectName: "track.addInstrument"
                        visible: info.title !== undefined
                        iconName: "music-notes"
                        iconColor: Theme.inkDim
                        menu: instruments
                        GlassMenu {
                            id: instruments
                            objectName: "track.addInstrument:menu"
                            Instantiator {
                                model: Registry.actionInfo("track.addInstrument").choices
                                delegate: ActionMenuItem {
                                    required property var modelData
                                    objectName: "track.addInstrument:" + modelData.id
                                    actionId: "track.addInstrument"
                                    title: modelData.title
                                    params: modelData.params
                                }
                                onObjectAdded: (index, object) => instruments.insertItem(index, object)
                                onObjectRemoved: (index, object) => instruments.removeItem(object)
                            }
                        }
                    }
                    ChromeButton {
                        objectName: "track.addMidi"
                        iconName: "piano-keys"
                        iconColor: Theme.inkDim
                    }
                    ChromeButton {
                        objectName: "track.addBus"
                        iconName: "arrows-merge"
                        iconColor: Theme.inkDim
                    }
                }
            }
        }
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
            required property real pan
            required property bool busRoutes
            required property string instrumentId
            required property var clips
            required property var trackData
            required property string laneComponent
            required property string headerComponent
            required property var fileDrop
            required property string clipType
            required property string automationMode
            required property var automation
            required property var automationParams
            required property var takePreview
            required property var takeFrozen
            readonly property var shownLanes: Nav.automationLanes[trackId] ?? ["gain"]
            readonly property string automationParam: shownLanes[0]
            readonly property var shownParam: automationParams.find(param => param.id === automationParam) ?? null
            readonly property var extraLanes: Nav.automationOpen
                ? shownLanes.slice(1).map(id => automationParams.find(param => param.id === id)).filter(param => param)
                : []
            readonly property bool dropTarget: area.dragRows !== 0 && index === area.dragFrom + area.dragRows
            readonly property color trackColor: Theme.trackColors[colorIndex % Project.trackColorCount]
            width: ListView.view.width
            height: Nav.trackHeight * (1 + extraLanes.length)

            Item {
                objectName: "lane:" + row.trackId
                x: area.headerWidth
                width: area.laneWidth
                height: Nav.trackHeight
                clip: true

                MouseArea {
                    objectName: "selection.clear:" + row.trackId
                    enabled: !Registry.readOnly
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: mouse => {
                        const keep = mouse.modifiers & Qt.ShiftModifier ? Array.from(Nav.selectedClips) : []
                        if (keep.length === 0) {
                            Registry.runAction("selection.clear", {})
                        }
                        Registry.runAction("track.select", {track: row.trackId})
                        if (Theme.isContextClick(mouse)) {
                            area.openLaneMenu()
                            return
                        }
                        band.begin(mapToItem(area, mouse.x, mouse.y), keep)
                    }
                    onPositionChanged: mouse => band.reach(mapToItem(area, mouse.x, mouse.y))
                    onReleased: band.end()
                    onCanceled: band.visible = false
                }

                Rectangle {
                    objectName: "lane.drop:" + row.trackId
                    anchors.fill: parent
                    visible: row.dropTarget
                    readonly property bool accepts: row.clipType === area.dragType
                    color: Theme.glow(accepts ? Theme.tape : Theme.rec)
                    border.width: Theme.selectionWidth
                    border.color: accepts ? Theme.tape : Theme.rec
                    z: 1
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
                            item.viewFrom = Qt.binding(() => area.scrollTicks)
                            item.viewTo = Qt.binding(() => area.scrollTicks + area.visibleTicks)
                        }
                    }
                    // We count the clips rather than list them, so an edit keeps the clip items and a drag survives it.
                    Repeater {
                        model: row.laneComponent === "" ? row.clips.length : 0
                        delegate: ClipItem {
                            required property int index
                            // A removed clip's item can see the shorter list before the Repeater drops the item.
                            clip: row.clips[index] ?? {id: "", type: "", start: 0, length: 0}
                            timeline: area
                            rowIndex: row.index
                            trackName: row.name
                            trackColor: row.trackColor
                            muted: row.mute
                            pixelsPerTick: area.pixelsPerTick
                            viewFrom: area.scrollTicks
                            viewTo: area.scrollTicks + area.visibleTicks
                            height: Nav.trackHeight
                        }
                    }
                    TakeRegion {
                        objectName: "takePreview:" + row.trackId
                        visible: row.laneComponent === "" && row.takePreview.type !== undefined
                        take: visible ? row.takePreview : {type: "", start: 0, length: 0, file: "", frames: 0, notes: [],
                                                          liveFrom: 0, lowKey: -1, highKey: -1}
                        frozen: visible ? row.takeFrozen : []
                        title: row.name + "   Take " + (row.clips.length + 1)
                        pixelsPerTick: area.pixelsPerTick
                        viewFrom: area.scrollTicks
                        viewTo: area.scrollTicks + area.visibleTicks
                        height: Nav.trackHeight
                    }
                }

                AutomationLane {
                    anchors.fill: parent
                    visible: Nav.automationOpen && row.shownParam !== null
                    trackId: row.trackId
                    trackColor: row.trackColor
                    param: row.shownParam ?? {id: "", title: "", minimum: 0, maximum: 1, value: 0, toggle: false}
                    points: row.automation.find(lane => lane.param === row.automationParam)?.points ?? []
                    reads: row.automationMode !== "off"
                    scrollTicks: area.scrollTicks
                    pixelsPerTick: area.pixelsPerTick
                }
            }

            TrackHeader {
                width: area.headerWidth
                height: Nav.trackHeight
                index: row.index
                trackId: row.trackId
                name: row.name
                icon: row.icon
                headerComponent: row.headerComponent
                trackColor: row.trackColor
                mute: row.mute
                solo: row.solo
                armed: row.armed
                monitor: row.monitor
                gainDb: row.gainDb
                pan: row.pan
                records: !row.busRoutes
                instrument: row.instrumentId !== ""
                automationMode: row.automationMode
                automationParams: row.automationParams
                automationParam: row.automationParam
                shownLanes: row.shownLanes
                trackList: area
                onDropIndexChanged: area.dropIndex = dropIndex
                onContextRequested: area.menuAtCursor = true
            }

            Repeater {
                model: row.extraLanes
                delegate: AutomationRow {
                    required property var modelData
                    required property int index
                    y: Nav.trackHeight * (index + 1)
                    width: row.width
                    height: Nav.trackHeight
                    headerWidth: area.headerWidth
                    trackId: row.trackId
                    trackColor: row.trackColor
                    param: modelData
                    params: row.automationParams
                    points: row.automation.find(lane => lane.param === modelData.id)?.points ?? []
                    reads: row.automationMode !== "off"
                    scrollTicks: area.scrollTicks
                    pixelsPerTick: area.pixelsPerTick
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.hairline
                color: Theme.rule
            }
        }
    }

    // A drag over empty lanes selects the clips it touches, as Logic's rubber band does; Shift adds to the selection.
    Rectangle {
        id: band
        objectName: "tracks.band"
        property point from
        property var kept: []
        visible: false
        color: Theme.bandFill
        border.width: Theme.hairline
        border.color: Theme.tape
        z: 1

        function begin(at, keep) {
            from = at
            kept = keep
        }
        function reach(at) {
            x = Math.max(area.headerWidth, Math.min(from.x, at.x))
            y = Math.max(area.lanesTop, Math.min(from.y, at.y))
            width = Math.min(area.width, Math.max(from.x, at.x)) - x
            height = Math.min(area.height, Math.max(from.y, at.y)) - y
            visible = visible || Math.abs(at.x - from.x) >= Theme.dragThreshold || Math.abs(at.y - from.y) >= Theme.dragThreshold
        }
        function end() {
            if (!visible) {
                return
            }
            visible = false
            const fromTicks = area.scrollTicks + (x - area.headerWidth) / area.pixelsPerTick
            const toTicks = fromTicks + width / area.pixelsPerTick
            const firstRow = area.rowAt(y)
            const lastRow = area.rowAt(y + height)
            const ids = Array.from(kept)
            for (let row = Math.max(0, firstRow); row <= Math.min(list.count - 1, lastRow); ++row) {
                const item = list.itemAtIndex(row)
                for (const clip of item ? item.clips : []) {
                    if (clip.start < toTicks && clip.end > fromTicks && ids.indexOf(clip.id) < 0) {
                        ids.push(clip.id)
                    }
                }
            }
            Registry.runAction("selection.set", {clips: ids})
        }
    }

    ToggleSweep {
        objectName: "tracks.toggleSweep"
        anchors.fill: list
        target: list
    }

    Rectangle {
        objectName: "tracks.dropIndicator"
        visible: area.dropIndex >= 0
        y: area.rowTop(area.dropIndex) - height / 2
        width: area.width
        height: Theme.selectionWidth
        color: Theme.ink
    }

    DropArea {
        id: fileDrop
        objectName: "tracks.fileDrop"
        enabled: !Registry.readOnly
        anchors.fill: list
        property int row: -1
        property int files: 0
        property real at: 0

        function follow(drag) {
            row = area.rowAt(drag.y + area.lanesTop)
            at = Project.placed(Math.round(area.scrollTicks + Math.max(0, drag.x - area.headerWidth) / area.pixelsPerTick),
                                Qt.NoModifier)
        }

        onEntered: drag => {
            drag.accepted = drag.hasUrls
            files = drag.urls.length
            follow(drag)
        }
        onPositionChanged: drag => follow(drag)
        onDropped: drop => {
            drop.acceptProposedAction()
            area.importFiles(drop.urls, row, at)
        }
    }

    Rectangle {
        objectName: "tracks.fileDropTarget"
        visible: fileDrop.containsDrag
        x: Math.max(area.headerWidth, area.xOf(fileDrop.at))
        y: area.rowTop(fileDrop.row)
        width: area.width - x
        height: fileDrop.files * Nav.trackHeight
        color: Theme.glow(Theme.tape)
        border.width: Theme.selectionWidth
        border.color: Theme.tape
    }

    Rectangle {
        x: 0
        width: area.headerWidth
        height: area.lanesTop
        color: Theme.slate
        ChromeButton {
            objectName: "edit.toggleSnap"
            visible: !Registry.readOnly
            y: (Theme.rulerHeight - height) / 2
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2
            implicitWidth: Theme.headerButton
            implicitHeight: Theme.headerButton
            iconName: "magnet"
            iconColor: Theme.inkDim
            active: Registry.settings["edit.snap"] === true
            activeColor: Theme.ink
            hint: "Hold " + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + " while dragging to snap the other way"
        }
        ChromeButton {
            objectName: "view.toggleAutomation"
            y: (Theme.rulerHeight - height) / 2
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2 * 2 + Theme.headerButton
            implicitWidth: Theme.headerButton
            implicitHeight: Theme.headerButton
            iconName: "automation"
            iconColor: Theme.inkDim
            active: Nav.automationOpen
            activeColor: Theme.ink
        }
        Text {
            y: Theme.rulerHeight
            height: Theme.markerLaneHeight
            leftPadding: Theme.space3
            verticalAlignment: Text.AlignVCenter
            text: "Markers"
            color: Theme.inkDim
            font.pixelSize: Theme.textSmall
        }
        Row {
            y: Theme.rulerHeight
            height: Theme.markerLaneHeight
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2
            Repeater {
                model: [{id: "marker.previous", icon: "caret-left", moves: true},
                    {id: "marker.next", icon: "caret-right", moves: true}, {id: "marker.toggleList", icon: "list"},
                    {id: "marker.add", icon: "plus"}].filter(button => button.moves || !Registry.readOnly)
                ChromeButton {
                    required property var modelData
                    objectName: modelData.id + ":lane"
                    actionId: modelData.id
                    implicitWidth: Theme.markerLaneHeight
                    implicitHeight: Theme.markerLaneHeight
                    iconName: modelData.icon
                    iconColor: Theme.inkDim
                    active: modelData.id === "marker.toggleList" && Nav.inspectorOpen && Nav.markerListOpen
                    activeColor: Theme.ink
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.hairline
            color: Theme.rule
        }
    }

    Ruler {
        x: area.headerWidth
        width: area.laneWidth
        height: Theme.rulerHeight
        scrollTicks: area.scrollTicks
        pixelsPerTick: area.pixelsPerTick
        barStep: area.barStep
        showBeats: area.showBeats
    }

    MarkerLane {
        x: area.headerWidth
        y: Theme.rulerHeight
        width: area.laneWidth
        height: Theme.markerLaneHeight
        scrollTicks: area.scrollTicks
        pixelsPerTick: area.pixelsPerTick
    }

    Rectangle {
        x: area.headerWidth - Theme.hairline
        width: Theme.hairline
        height: area.height
        color: Theme.rule
    }

    // We keep the edge above the tracks, so the header meters and the clip edges at bar 1 keep their clicks.
    PaneEdge {
        visible: !Registry.readOnly
        settingId: "view.headerWidth"
        x: area.headerWidth - width / 2
        height: area.lanesTop
    }

    Item {
        objectName: "playhead"
        readonly property real at: area.xOf(Project.positionTicks)
        x: at - width / 2
        width: Theme.playheadCap
        height: area.height
        visible: list.count > 0 && at >= area.headerWidth && at <= area.width
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
        x: area.headerWidth
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

    Label {
        anchors.centerIn: parent
        visible: list.count === 0 && !Registry.readOnly
        text: "Add a track to start recording"
        color: Theme.inkDim
        font.pixelSize: Theme.textTitle
    }
}
