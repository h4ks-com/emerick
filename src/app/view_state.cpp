#include "emerick/app/view_state.hpp"

namespace emerick::app {

namespace {

QStringList savedLanes(const Json& params) {
    QStringList lanes;
    if (!params.is_array()) {
        return lanes;
    }
    for (const Json& param : params) {
        if (param.is_string()) {
            lanes.append(qs(param.get<std::string>()));
        }
    }
    return lanes;
}

}  // namespace

Json viewState(const Nav& nav) {
    return {{"pixelsPerTick", nav.pixelsPerTick()},
            {"scrollTicks", nav.scrollTicks()},
            {"trackHeight", nav.trackHeight()},
            {"automationOpen", nav.automationOpen()},
            {"automationLanes", nav.automationLanesJson()}};
}

void applyViewState(Nav& nav, const Json& state) {
    if (!state.is_object()) {
        return;
    }
    if (const auto found = state.find("pixelsPerTick"); found != state.end() && found->is_number()) {
        nav.setPixelsPerTick(found->get<double>());
    }
    if (const auto found = state.find("scrollTicks"); found != state.end() && found->is_number()) {
        nav.setScrollTicks(found->get<double>());
    }
    if (const auto found = state.find("trackHeight"); found != state.end() && found->is_number()) {
        nav.setTrackHeight(found->get<int>());
    }
    if (const auto found = state.find("automationOpen"); found != state.end() && found->is_boolean()) {
        nav.setAutomationOpen(found->get<bool>());
    }
    if (const auto found = state.find("automationLanes"); found != state.end() && found->is_object()) {
        QVariantMap lanes;
        for (const auto& [track, params] : found->items()) {
            if (const QStringList shown = savedLanes(params); !shown.isEmpty()) {
                lanes.insert(qs(track), shown);
            }
        }
        nav.setAutomationLanes(lanes);
    }
}

}  // namespace emerick::app
