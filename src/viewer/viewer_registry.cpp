#include "viewer_registry.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <string_view>

#include "emerick/core/lookup.hpp"
#include "emerick/model/automation.hpp"

namespace emerick::viewer {

namespace {

struct ViewerAction {
    std::string_view id;
    std::string_view title;
    std::string_view shortcut;
    void (ViewerRegistry::*run)(const QVariantMap&);
};

constexpr std::array kActions{
    ViewerAction{"transport.locate", "Move playhead", "", &ViewerRegistry::locate},
    ViewerAction{"transport.togglePlay", "Play or pause", "Space", &ViewerRegistry::togglePlay},
    ViewerAction{"view.toggleAutomation", "Show or hide automation", "A", &ViewerRegistry::toggleAutomation},
    ViewerAction{"automation.showLane", "Show automation lane", "", &ViewerRegistry::showLane},
    ViewerAction{"marker.previous", "Go to previous marker", "Alt+,", &ViewerRegistry::previousMarker},
    ViewerAction{"marker.next", "Go to next marker", "Alt+.", &ViewerRegistry::nextMarker},
};

QString text(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }

const ViewerAction* findAction(const QString& id) {
    return core::findEntry(kActions, std::string_view{id.toStdString()}, &ViewerAction::id);
}

}  // namespace

ViewerRegistry::ViewerRegistry(const ViewerProject& project, app::Nav& nav, QObject* parent)
    : QObject(parent), project_(project), nav_(nav) {}

QVariantMap ViewerRegistry::actionInfo(const QString& id) {
    const ViewerAction* action = findAction(id);
    return {{"title", action != nullptr ? text(action->title) : QString{}},
            {"shortcut", action != nullptr ? text(action->shortcut) : QString{}},
            {"enabled", action != nullptr},
            {"choices", model::kAutomationModes | std::views::transform([](const model::AutomationModeName& mode) {
                            return QVariant{QVariantMap{{"id", text(mode.id)}, {"title", text(mode.title)}}};
                        }) | std::ranges::to<QVariantList>()}};
}

void ViewerRegistry::activate(const QString& id) { runAction(id, {}); }

QVariant ViewerRegistry::runAction(const QString& id, const QVariantMap& params) {
    if (const ViewerAction* action = findAction(id)) {
        (this->*action->run)(params);
    }
    return {};
}

void ViewerRegistry::locate(const QVariantMap& params) { seekToTicks(params.value("at").toLongLong()); }

void ViewerRegistry::togglePlay(const QVariantMap& /*params*/) { emit playToggled(); }

void ViewerRegistry::toggleAutomation(const QVariantMap& /*params*/) { nav_.setAutomationOpen(!nav_.automationOpen()); }

void ViewerRegistry::showLane(const QVariantMap& params) {
    QVariantMap lanes = nav_.automationLanes();
    lanes.insert(params.value("track").toString(), params.value("param"));
    nav_.setAutomationLanes(lanes);
    nav_.setAutomationOpen(true);
}

void ViewerRegistry::previousMarker(const QVariantMap& /*params*/) {
    const std::vector<core::Ticks> ats = markerTicks();
    const auto later = std::ranges::lower_bound(ats, project_.positionTicks());
    seekToTicks(later == ats.begin() ? 0 : *std::prev(later));
}

void ViewerRegistry::nextMarker(const QVariantMap& /*params*/) {
    const std::vector<core::Ticks> ats = markerTicks();
    if (const auto next = std::ranges::upper_bound(ats, project_.positionTicks()); next != ats.end()) {
        seekToTicks(*next);
    }
}

std::vector<core::Ticks> ViewerRegistry::markerTicks() const {
    std::vector<core::Ticks> ats =
        project_.document().markers | std::views::transform(&model::Marker::at) | std::ranges::to<std::vector>();
    std::ranges::sort(ats);
    return ats;
}

void ViewerRegistry::seekToTicks(qint64 ticks) { emit seekRequested(project_.document().tempo.ticksToSeconds(ticks)); }

}  // namespace emerick::viewer
