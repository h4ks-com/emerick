import QtQuick
import Emerick.Ui
import Emerick.Yamaha

WarningBadge {
    property string trackId
    objectName: "yamaha.keyboard.check:" + trackId
    visible: message !== ""
    message: KeyboardCheck.message
}
