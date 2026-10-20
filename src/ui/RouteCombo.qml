import QtQuick
import Emerick.App

// One part of a track route: the device, or with part "channel" its channels. Picking runs the route action with
// only the params that entry sets, and the action keeps the rest of the route.
SearchCombo {
    required property string actionId
    required property string trackId
    property string part
    objectName: actionId + ":" + trackId + (part ? ":" + part : "")
    missingText: part ? currentId : "Missing device"
    toolTip: Registry.actionInfo(actionId).title
    onPicked: entry => Registry.runAction(actionId, Object.assign({track: trackId}, entry.params))
}
