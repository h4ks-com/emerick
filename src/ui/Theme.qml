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
    readonly property color mute: "#4A86C8"
    readonly property color glass: Qt.rgba(slate.r, slate.g, slate.b, 0.96)
    readonly property color highlight: Qt.rgba(1, 1, 1, 0.06)
    // Text buttons lighten whatever row they sit on, so they stay visible on a selected row too.
    readonly property color buttonFill: Qt.rgba(1, 1, 1, 0.07)
    readonly property color buttonHover: Qt.rgba(1, 1, 1, 0.13)
    readonly property color buttonDown: Qt.rgba(1, 1, 1, 0.2)
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
    readonly property int headerHeight: 56
    readonly property int listRowHeight: 36
    readonly property int swatchSize: 24
    readonly property int switchWidth: 36
    readonly property int snapDuration: 120
    readonly property int inspectorWidth: 260
    readonly property int trackHeaderWidth: 240
    readonly property int sidebarWidth: 200
    readonly property int labelWidth: 160
    readonly property int paletteWidth: 560
    readonly property int menuWidth: 240
    readonly property int channelPickerWidth: 76
    readonly property int pickerRows: 10
    readonly property int paletteTop: 72
    readonly property int paletteListHeight: 360
    readonly property int windowWidth: 1280
    readonly property int windowHeight: 800
    readonly property real mutedOpacity: 0.4

    readonly property string monoFamily: Qt.platform.os === "osx" ? "Menlo" : "monospace"
    // Lua syntax colors, muted like the track colors so code reads calmly next to the timeline.
    readonly property var codeColors: ({
        keyword: "#C38FD4",
        stringLiteral: "#9CC07A",
        numberLiteral: "#E0956A",
        comment: "#6B707A",
        call: "#6FC2CF",
        api: "#6FA0E0",
        actionId: "#D88AA0"
    })
    readonly property real dockShare: 0.6
    readonly property int consoleHeight: 240
    readonly property int consoleMinHeight: 96
    readonly property int consoleInputLines: 8
    readonly property int completionWidth: 520
    readonly property int completionRows: 8
    readonly property int splitterGrip: 6
    readonly property int mixerHeight: 320
    readonly property int mixerMinHeight: 200
    readonly property int stripWidth: 84
    readonly property int faderMinHeight: 64
    readonly property int faderGroove: 3
    readonly property int faderCapWidth: 28
    readonly property int faderCapHeight: 14
    // Level meters turn from meterSafe to meterHot between these levels and reach rec at full scale.
    readonly property color meterSafe: "#5FA35A"
    readonly property color meterHot: "#E0C341"
    readonly property real meterSafeDb: -18
    readonly property real meterHotDb: -6
    readonly property int meterBar: 2
    readonly property int meterGap: 1
    readonly property int meterClipLight: 4

    readonly property int transportButton: 36
    readonly property int displayHeight: 44
    readonly property int tooltipDelay: 500
    readonly property int noticeDuration: 10000
    readonly property color displayInset: Qt.rgba(0, 0, 0, 0.35)
    function glow(color) {
        return Qt.rgba(color.r, color.g, color.b, 0.18)
    }

    readonly property int rulerHeight: 28
    readonly property int markerLaneHeight: 20
    readonly property int markerListRows: 8
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
    readonly property int dragPixelsPerStep: 4
    readonly property real dragFineFactor: 0.1
    readonly property int dragCoarseSteps: 10
    readonly property int sweepSampleSpacing: 4
    readonly property real gainMinDb: -60
    readonly property real gainMaxDb: 6
    readonly property real gainStepDb: 0.5
    readonly property real clipGainMaxDb: 24
    readonly property real panStep: 0.01
    readonly property real panKnobStep: 0.02
    readonly property int panKnob: 20
    readonly property real panKnobSweep: 135
    readonly property real minBpm: 20
    readonly property real maxBpm: 400
    readonly property real numberSettingStep: 0.1
    readonly property int numberSettingDecimals: 2
    readonly property int gainReadoutWidth: 64
    readonly property real wheelStep: 0.5
    readonly property real wheelZoom: 0.002
    readonly property real wheelTrackHeight: 0.1
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

    readonly property real paramStep: 0.004
    readonly property int effectKnob: 36
    readonly property int knobRing: 3
    readonly property int effectKnobWidth: 76
    readonly property int effectKnobHeight: 80
    readonly property int eqBands: 7
    readonly property int eqBandParams: 4
    readonly property int eqBandWidth: 84
    readonly property int eqGraphHeight: 220
    readonly property int eqPoint: 12
    readonly property int eqCurveSpacing: 2
    readonly property real eqCurveWidth: 1.5
    readonly property real eqRangeDb: 24
    readonly property real eqMinHz: 20
    readonly property real eqMaxHz: 20000
    readonly property real eqWheelQ: 1.15
    readonly property color eqFill: Qt.rgba(1, 1, 1, 0.08)
    readonly property real reductionRangeDb: 20

    readonly property int pianoRollHeight: 320
    readonly property int pianoRollMinHeight: 200
    readonly property int velocityLaneHeight: 64
    readonly property real velocityLaneShare: 0.2
    readonly property int velocityBar: 4
    readonly property int noteGrip: 6
    readonly property int minGridSpacing: 6
    readonly property color whiteKey: "#D9DADD"
    readonly property color blackKey: "#202125"
    readonly property real blackKeyLength: 0.6
    readonly property color blackKeyRow: Qt.rgba(0, 0, 0, 0.22)
    readonly property color outsideClip: Qt.rgba(0, 0, 0, 0.35)
    readonly property color bandFill: Qt.rgba(1, 1, 1, 0.08)
    // Notes run from blue at the softest velocity through green and amber to red at the loudest, as in Logic.
    function velocityColor(velocity) {
        return Qt.hsla(0.62 * (1 - Math.max(0, velocity - 1) / 126), 0.55, 0.55, 1)
    }
    // A selected note keeps its velocity hue but turns pale, because an amber outline vanishes on the loud notes.
    function selectedNote(velocity) {
        return Qt.hsla(0.62 * (1 - Math.max(0, velocity - 1) / 126), 0.55, 0.82, 1)
    }

    // Track icons are Phosphor names and fade icons are our own; an icon we do not bundle shows as a note.
    readonly property var icons: ["arrows-merge", "automation", "broom", "circle", "copy", "dots-three", "fade-equalPower", "fade-exponential", "fade-linear", "fade-logarithmic", "fade-sCurve", "file", "gear-six", "guitar", "magnet", "magnifying-glass", "metronome", "music-note", "music-notes", "pause", "pencil-simple", "piano-keys", "play", "plus", "sidebar-simple", "skip-back", "sliders", "squares-four", "stop", "waveform", "x"]
    function iconSource(name) {
        return "icons/" + (icons.indexOf(name) >= 0 ? name : "music-note") + ".svg"
    }

    // A right click, or a Control click on macOS, where Qt reports the Control key as Meta.
    function isContextClick(mouse) {
        return mouse.button === Qt.RightButton
            || (Qt.platform.os === "osx" && mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.MetaModifier))
    }

    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
    }

    // Styled text that bolds the matched [begin, end) ranges of text, as the fuzzy matcher reports them.
    function highlighted(text, ranges) {
        let html = ""
        let last = 0
        for (const range of ranges) {
            html += escaped(text.slice(last, range[0])) + "<b>" + escaped(text.slice(range[0], range[1])) + "</b>"
            last = range[1]
        }
        return html + escaped(text.slice(last))
    }

    // Registry shortcuts use Qt's portable "Ctrl+" spelling, which macOS maps to Command and writes as ⇧⌘S.
    function shortcutText(sequence) {
        if (Qt.platform.os !== "osx") {
            return sequence
        }
        const keys = sequence.split("+")
        const key = keys.pop() || "+"
        return (keys.includes("Meta") ? "⌃" : "") + (keys.includes("Alt") ? "⌥" : "") + (keys.includes("Shift") ? "⇧" : "")
            + (keys.includes("Ctrl") ? "⌘" : "") + (macKeys[key] ?? key)
    }
    readonly property var macKeys: ({Left: "←", Right: "→", Up: "↑", Down: "↓", Return: "↩", Enter: "⌅", Backspace: "⌫",
                                     Del: "⌦", Delete: "⌦", Esc: "⎋", Escape: "⎋", Tab: "⇥", Home: "↖", End: "↘",
                                     PgUp: "⇞", PgDown: "⇟"})
}
