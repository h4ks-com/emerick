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

    // Drag previews in ticks; -1 shows the clip as stored. shift moves the material along with a dragged clip.
    property real previewStart: -1
    property real previewEnd: -1
    property real previewFadeIn: -1
    property real previewFadeOut: -1
    property real shift: 0

    readonly property string clipId: clip.id
    readonly property bool audio: clip.type === "audio"
    readonly property real start: previewStart >= 0 ? previewStart : clip.start
    readonly property real end: previewEnd >= 0 ? previewEnd : clip.start + clip.length
    readonly property real fadeIn: audio ? (previewFadeIn >= 0 ? previewFadeIn : clip.fadeIn) : 0
    readonly property real fadeOut: audio ? (previewFadeOut >= 0 ? previewFadeOut : clip.fadeOut) : 0
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

    // Alt places freely; otherwise the position snaps when the edit.snap setting is on.
    function place(ticks, modifiers) {
        const rounded = Math.round(ticks)
        return modifiers & Qt.AltModifier ? Math.max(0, rounded) : Project.snapped(rounded)
    }

    function selectOnPress(modifiers) {
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

    function clearPreview() {
        previewStart = -1
        previewEnd = -1
        previewFadeIn = -1
        previewFadeOut = -1
        shift = 0
    }

    // We clear the preview first, because the action rebuilds this track's clips and with them this item.
    function commit(actionId, params) {
        clearPreview()
        Registry.runAction(actionId, Object.assign({clip: clipId}, params))
    }

    Loader {
        anchors.fill: parent
        active: root.inView
        sourceComponent: ClipBody {
            clipItem: root
        }
    }
}
