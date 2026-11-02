#pragma once

#include <array>
#include <numbers>

#include "emerick/app/nav.hpp"
#include "emerick/core/time.hpp"

namespace emerick::app {

// The actions that only look around a project, with the keys the app gives them. The app registers them on its
// registry and the read-only viewer runs them itself, so both answer to the same keys.
struct ActionKeys {
    const char* id;
    const char* title;
    const char* shortcut;
    const char* extraShortcut;
};

inline constexpr ActionKeys kTogglePlayKeys{"transport.togglePlay", "Play or pause", "Space", ""};
inline constexpr ActionKeys kStopKeys{"transport.stop", "Stop", "", ""};
inline constexpr ActionKeys kGotoStartKeys{"transport.gotoStart", "Go to beginning", "Return", "Home"};
inline constexpr ActionKeys kGotoEndKeys{"transport.gotoEnd", "Go to end", "End", ""};
inline constexpr ActionKeys kToggleAutomationKeys{"view.toggleAutomation", "Show or hide automation", "A", ""};
inline constexpr ActionKeys kZoomToFitKeys{"view.zoomToFit", "Zoom to fit", "Z", ""};

struct BarStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    const char* extraShortcut;
    int bars;
};

inline constexpr std::array kBarSteps{
    BarStep{"transport.barBack", "Go back one bar", "rewind one bar", "Left", ",", -1},
    BarStep{"transport.barForward", "Go forward one bar", "forward one bar", "Right", ".", 1},
    BarStep{"transport.eightBarsBack", "Go back eight bars", "rewind eight bars", "Shift+Left", "Shift+,", -8},
    BarStep{"transport.eightBarsForward", "Go forward eight bars", "forward eight bars", "Shift+Right", "Shift+.", 8},
};

struct ZoomStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    const char* extraShortcut;
    double factor;
};

inline constexpr std::array kZoomSteps{
    ZoomStep{"view.zoomIn", "Zoom in", "wider timeline", "Ctrl+Right", "Ctrl+=", std::numbers::sqrt2},
    ZoomStep{"view.zoomOut", "Zoom out", "narrower timeline", "Ctrl+Left", "Ctrl+-", 1 / std::numbers::sqrt2},
};

struct HeightStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    int height;
};

inline constexpr int kTrackHeightStep = 12;

inline constexpr std::array kHeightSteps{
    HeightStep{"view.trackTaller", "Make tracks taller", "vertical zoom in", "Ctrl+Down", kTrackHeightStep},
    HeightStep{"view.trackShorter", "Make tracks shorter", "vertical zoom out", "Ctrl+Up", -kTrackHeightStep},
};

struct MarkerStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    int step;
};

inline constexpr std::array kMarkerSteps{
    MarkerStep{"marker.next", "Go to next marker", "next section", "Alt+.", 1},
    MarkerStep{"marker.previous", "Go to previous marker", "previous section", "Alt+,", -1},
};

// Steps land on bar lines, so going back from inside a bar first returns to that bar's start, as in Logic.
core::Ticks barStep(core::Ticks position, core::Ticks ticksPerBar, int bars);
// Zooming keeps the playhead in place while it is in view, else the middle of the view.
double zoomAnchor(core::Ticks playhead, const Nav& nav);
// Zooms and scrolls so the span from from to to fills the lanes with a small margin, and spans at least minimum.
void fitSpan(Nav& nav, core::Ticks from, core::Ticks to, core::Ticks minimum);

}  // namespace emerick::app
