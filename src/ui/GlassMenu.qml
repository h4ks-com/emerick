import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Menu {
    // A picker's menu checks its current choice, so every row leaves room for the check.
    property bool marks: false
    cascade: true
    padding: Theme.space1
    // A menu grows to its widest item, so a long title or shortcut never clips.
    contentWidth: {
        let widest = 0
        for (let at = 0; at < count; ++at) {
            const item = itemAt(at)
            widest = Math.max(widest, item ? item.implicitWidth : 0)
        }
        return widest
    }
    background: GlassPanel {
        implicitWidth: Theme.menuWidth
    }
    delegate: GlassMenuItem {}
}
