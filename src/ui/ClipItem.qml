import QtQuick
import Emerick.App

// A clip's place on the timeline. Its body loads only while the clip is in view, so a track of many clips stays light.
Item {
    id: root

    property var clip
    // The tracks area, which opens the clip menu and tracks a drag across rows, and this clip's row there.
    property Item timeline
    property int rowIndex
    property string trackName
    property color trackColor
    property bool muted
    property real pixelsPerTick
    property real viewFrom
    property real viewTo
    property bool editing: false

    readonly property string clipId: clip.id
    readonly property bool audio: clip.type === "audio"
    readonly property real start: clip.start
    readonly property real end: clip.start + clip.length
    readonly property real fadeIn: audio ? clip.fadeIn : 0
    readonly property real fadeOut: audio ? clip.fadeOut : 0
    readonly property string fadeInShape: audio ? clip.fadeInShape : "linear"
    readonly property string fadeOutShape: audio ? clip.fadeOutShape : "linear"
    readonly property real fadeInCurve: audio ? clip.fadeInCurve : 0
    readonly property real fadeOutCurve: audio ? clip.fadeOutCurve : 0
    readonly property bool selected: Nav.selectedClips.indexOf(clipId) >= 0
    readonly property bool inView: end > viewFrom && start < viewTo

    objectName: "clip:" + clipId
    x: start * pixelsPerTick
    width: Math.max(2 * Theme.hairline, (end - start) * pixelsPerTick)
    visible: inView
    opacity: muted ? Theme.mutedOpacity : 1
    z: editing ? 1 : 0

    function sceneX(area, mouse) {
        return area.mapToItem(null, mouse.x, 0).x
    }

    function place(ticks, modifiers) {
        return Project.placed(Math.round(ticks), modifiers)
    }

    function selectOnPress(modifiers) {
        Registry.runAction("track.select", {track: timeline.trackIdAt(rowIndex)})
        if (modifiers & Qt.ShiftModifier) {
            const ids = Array.from(Nav.selectedClips)
            const at = ids.indexOf(clipId)
            if (at >= 0) {
                ids.splice(at, 1)
            } else {
                ids.push(clipId)
            }
            Registry.runAction("selection.set", {clips: ids})
        } else if (!selected) {
            Registry.runAction("selection.set", {clips: [clipId]})
        }
    }

    // Each call replaces the drag's last edit, so the clip and every view of it follow the drag as one undo step.
    function drag(actionId, params) {
        Registry.updateGesture(Registry.actionInfo(actionId).title,
                               [{id: actionId, params: Object.assign({clip: clipId}, params)}])
    }

    function cancelDrag() {
        Registry.cancelGesture()
        Registry.endGesture()
    }

    Loader {
        anchors.fill: parent
        active: root.inView
        sourceComponent: ClipBody {
            clipItem: root
        }
    }
}
