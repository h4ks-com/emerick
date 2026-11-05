#include "emerick/app/view_state.hpp"

namespace emerick::app {

Json viewState(const Nav& nav) {
    Json lanes = Json::object();
    const QVariantMap shown = nav.automationLanes();
    for (auto lane = shown.cbegin(); lane != shown.cend(); ++lane) {
        lanes[lane.key().toStdString()] = lane.value().toString().toStdString();
    }
    return {{"pixelsPerTick", nav.pixelsPerTick()},
            {"scrollTicks", nav.scrollTicks()},
            {"trackHeight", nav.trackHeight()},
            {"automationOpen", nav.automationOpen()},
            {"automationLanes", std::move(lanes)}};
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
        for (const auto& [track, param] : found->items()) {
            if (param.is_string()) {
                lanes.insert(qs(track), qs(param.get<std::string>()));
            }
        }
        nav.setAutomationLanes(lanes);
    }
}

}  // namespace emerick::app
