import QtQuick
import QtQuick.Controls.Basic
import Emerick.App

Menu {
    cascade: true
    padding: Theme.space1
    background: GlassPanel {
        implicitWidth: Theme.menuWidth
    }
    delegate: GlassMenuItem {}
}
