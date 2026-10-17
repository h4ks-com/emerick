pragma Singleton
import QtQuick

// Design tokens from docs/design.md. Views read colors and sizes only from here.
QtObject {
    readonly property color graphite: "#1B1C20"
    readonly property color slate: "#25272D"
    readonly property color lift: "#30333A"
    readonly property color rule: "#3B3E46"
    readonly property color ink: "#E6E7EA"
    readonly property color inkDim: "#9599A3"
    readonly property color tape: "#F2B544"
    readonly property color rec: "#EF5350"
    readonly property color glass: Qt.rgba(slate.r, slate.g, slate.b, 0.88)
    readonly property color highlight: Qt.rgba(1, 1, 1, 0.06)
    readonly property color clear: "transparent"

    readonly property var trackColors: ["#3FA59A", "#4A86C8", "#8A6CC9", "#C8607E", "#D59A3C", "#5FA35A", "#B59B76", "#7A8796"]

    readonly property int radiusClip: 6
    readonly property int radiusWindow: 10

    readonly property int textSmall: 11
    readonly property int textLabel: 12
    readonly property int textBody: 13
    readonly property int textTitle: 15
    readonly property int textCounter: 28

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24

    readonly property int hairline: 1
    readonly property int buttonHeight: 28
    readonly property int headerHeight: 52
    readonly property int rowHeight: 48
    readonly property int listRowHeight: 36
    readonly property int swatchSize: 24
    readonly property int inspectorWidth: 260
    readonly property int trackHeaderWidth: 240
    readonly property int sidebarWidth: 200
    readonly property int labelWidth: 160
    readonly property int paletteWidth: 560
    readonly property int paletteTop: 72
    readonly property int paletteListHeight: 360
    readonly property int windowWidth: 1280
    readonly property int windowHeight: 800
    readonly property real mutedOpacity: 0.4

    readonly property int rulerHeight: 28
    readonly property int colorStripe: 4
    readonly property int iconSize: 18
    readonly property int headerButton: 20
    readonly property int clipLabelHeight: 16
    readonly property int clipGrip: 6
    readonly property int fadeHandle: 8
    readonly property int selectionWidth: 2
    readonly property int playheadCap: 10
    readonly property int labelSpacing: 64
    readonly property int beatSpacing: 12
    readonly property int maxTextureWidth: 4096
    readonly property int dragThreshold: 4
    readonly property real gainMinDb: -60
    readonly property real gainMaxDb: 6
    readonly property real wheelStep: 0.5
    readonly property real wheelZoom: 0.002
    readonly property real followLead: 0.1
    readonly property real followEdge: 0.95
    readonly property int timelineTailBars: 16
    readonly property color clipLabel: Qt.rgba(0, 0, 0, 0.28)
    readonly property color clipInk: Qt.rgba(0, 0, 0, 0.5)
    readonly property color fadeShade: Qt.rgba(0, 0, 0, 0.3)
    readonly property color fadeLine: Qt.rgba(1, 1, 1, 0.7)
    readonly property int clipInset: 2
    readonly property color barLine: Qt.rgba(1, 1, 1, 0.07)
    readonly property color beatLine: Qt.rgba(1, 1, 1, 0.03)

    // Track icons are Phosphor names; an icon we do not bundle shows as a note.
    readonly property var icons: ["guitar", "music-note", "music-notes", "piano-keys", "squares-four", "waveform"]
    function iconSource(name) {
        return "icons/" + (icons.indexOf(name) >= 0 ? name : "music-note") + ".svg"
    }

    // Registry shortcuts use Qt's portable "Ctrl+" spelling, which macOS maps to Command.
    function shortcutText(sequence) {
        return Qt.platform.os === "osx" ? sequence.replace("Ctrl+", "⌘") : sequence
    }
}
