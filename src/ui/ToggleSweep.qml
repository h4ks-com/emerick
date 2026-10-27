import QtQuick
import Emerick.App

// Press a toggle inside target and drag across others of its group to give them all the pressed toggle's new
// state, as one undo step, the way Logic and Blender sweep mute buttons. A toggle joins with a sweepGroup, the
// sweepActionId and sweepParams that set it with a value param, its active state and a sweepState for the preview.
MouseArea {
    id: sweep
    required property Item target
    property string group
    property bool value
    property var touched: []
    property point last
    // A MouseArea shows the arrow by default, which would hide the cursors of the items under the sweep.
    cursorShape: undefined

    function toggleAt(item, point) {
        for (let index = 0; index < item.children.length; ++index) {
            const child = item.children[index]
            if (!child.visible || !child.enabled) {
                continue
            }
            const local = child.mapFromItem(sweep, point.x, point.y)
            if (!child.contains(local)) {
                continue
            }
            if (child.sweepGroup !== undefined && child.sweepGroup !== "") {
                return child
            }
            const found = toggleAt(child, point)
            if (found !== null) {
                return found
            }
        }
        return null
    }

    function touch(toggle) {
        if (toggle !== null && toggle.sweepGroup === group && touched.indexOf(toggle) < 0) {
            toggle.sweepState = value ? 1 : 0
            touched = touched.concat([toggle])
        }
    }

    onPressed: mouse => {
        const toggle = toggleAt(target, Qt.point(mouse.x, mouse.y))
        if (toggle === null) {
            mouse.accepted = false
            return
        }
        last = Qt.point(mouse.x, mouse.y)
        group = toggle.sweepGroup
        value = !toggle.active
        touched = []
        touch(toggle)
    }
    // We sample the whole path since the last move, so a fast drag still touches every toggle it crosses.
    onPositionChanged: mouse => {
        const samples = Math.max(1, Math.ceil(Math.hypot(mouse.x - last.x, mouse.y - last.y) / Theme.sweepSampleSpacing))
        for (let sample = 1; sample <= samples; ++sample) {
            const at = Qt.point(last.x + (mouse.x - last.x) * sample / samples, last.y + (mouse.y - last.y) * sample / samples)
            touch(toggleAt(target, at))
        }
        last = Qt.point(mouse.x, mouse.y)
    }
    onReleased: {
        const calls = touched.map(toggle => ({
            id: toggle.sweepActionId,
            params: Object.assign({}, toggle.sweepParams, {value: sweep.value})
        }))
        for (const toggle of touched) {
            toggle.sweepState = -1
        }
        touched = []
        if (calls.length > 0) {
            Registry.runActions(Registry.actionInfo(calls[0].id).title, calls)
        }
    }
    onCanceled: {
        for (const toggle of touched) {
            toggle.sweepState = -1
        }
        touched = []
    }
}
