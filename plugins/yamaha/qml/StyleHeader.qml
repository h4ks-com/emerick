import QtQuick
import Emerick.Yamaha

WarningBadge {
    property string trackId
    objectName: "yamaha.keyboard.check:" + trackId
    message: KeyboardCheck.message
}
