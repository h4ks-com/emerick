import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Menu {
    padding: Theme.space1
    background: GlassPanel {
        implicitWidth: Theme.menuWidth
    }
    delegate: GlassMenuItem {}
}
