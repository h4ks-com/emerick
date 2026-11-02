#include "emerick/app/view_actions.hpp"

#include <algorithm>

namespace emerick::app {

namespace {

constexpr double kFitMargin = 0.05;

}  // namespace

core::Ticks barStep(core::Ticks position, core::Ticks ticksPerBar, int bars) {
    const core::Ticks bar = position / ticksPerBar;
    const bool onLine = position % ticksPerBar == 0;
    const core::Ticks target = bars < 0 && !onLine ? bar + bars + 1 : bar + bars;
    return std::max<core::Ticks>(0, target * ticksPerBar);
}

double zoomAnchor(core::Ticks playhead, const Nav& nav) {
    const auto at = static_cast<double>(playhead);
    const double x = (at - nav.scrollTicks()) * nav.pixelsPerTick();
    return x >= 0 && x <= nav.laneWidth() ? at : nav.scrollTicks() + nav.laneWidth() / 2 / nav.pixelsPerTick();
}

void fitSpan(Nav& nav, core::Ticks from, core::Ticks to, core::Ticks minimum) {
    const auto span = static_cast<double>(std::max(to - from, minimum));
    if (nav.laneWidth() > 0) {
        nav.setPixelsPerTick(nav.laneWidth() / (span * (1 + 2 * kFitMargin)));
    }
    nav.setScrollTicks(static_cast<double>(from) - span * kFitMargin);
}

}  // namespace emerick::app
