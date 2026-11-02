#include "viewer_registry.hpp"

#include <QStringList>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <utility>

#include "emerick/app/view_actions.hpp"
#include "emerick/model/automation.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::viewer {

namespace {

QStringList keys(const char* shortcut, const char* extra = "") {
    return {QString::fromUtf8(shortcut), QString::fromUtf8(extra)};
}

QVariantList automationModes() {
    return model::kAutomationModes | std::views::transform([](const model::AutomationModeName& mode) {
               return QVariant{QVariantMap{
                   {"id", QString::fromUtf8(mode.id.data(), static_cast<qsizetype>(mode.id.size()))},
                   {"title", QString::fromUtf8(mode.title.data(), static_cast<qsizetype>(mode.title.size()))}}};
           }) |
           std::ranges::to<QVariantList>();
}

}  // namespace

ViewerRegistry::ViewerRegistry(const ViewerProject& project, app::Nav& nav, QObject* parent)
    : QObject(parent), project_(project), nav_(nav) {
    using app::kGotoEndKeys, app::kGotoStartKeys, app::kStopKeys, app::kToggleAutomationKeys, app::kTogglePlayKeys,
        app::kZoomToFitKeys;
    addAction(kTogglePlayKeys.id, kTogglePlayKeys.title, keys(kTogglePlayKeys.shortcut),
              [this](const QVariantMap&) { emit playToggled(); });
    addAction(kStopKeys.id, kStopKeys.title, {}, [this](const QVariantMap&) {
        if (project_.playing()) {
            emit pauseRequested();
        } else {
            seekToTicks(0);
        }
    });
    addAction(kGotoStartKeys.id, kGotoStartKeys.title, keys(kGotoStartKeys.shortcut, kGotoStartKeys.extraShortcut),
              [this](const QVariantMap&) { seekToTicks(0); });
    addAction(kGotoEndKeys.id, kGotoEndKeys.title, keys(kGotoEndKeys.shortcut),
              [this](const QVariantMap&) { seekToTicks(model::projectEnd(project_.document())); });
    addAction("transport.locate", "Go to position", {},
              [this](const QVariantMap& params) { seekToTicks(params.value("at").toLongLong()); });
    for (const app::BarStep& step : app::kBarSteps) {
        addAction(step.id, step.title, keys(step.shortcut, step.extraShortcut), [this, step](const QVariantMap&) {
            seekToTicks(app::barStep(project_.positionTicks(), project_.ticksPerBar(), step.bars));
        });
    }
    for (const app::MarkerStep& step : app::kMarkerSteps) {
        addAction(step.id, step.title, keys(step.shortcut),
                  [this, step](const QVariantMap&) { stepToMarker(step.step); });
    }
    for (const app::ZoomStep& step : app::kZoomSteps) {
        addAction(step.id, step.title, keys(step.shortcut, step.extraShortcut), [this, step](const QVariantMap&) {
            nav_.zoomAround(step.factor, app::zoomAnchor(project_.positionTicks(), nav_));
        });
    }
    addAction(kZoomToFitKeys.id, kZoomToFitKeys.title, keys(kZoomToFitKeys.shortcut), [this](const QVariantMap&) {
        app::fitSpan(nav_, 0, model::projectEnd(project_.document()), project_.ticksPerBar());
    });
    for (const app::HeightStep& step : app::kHeightSteps) {
        addAction(step.id, step.title, keys(step.shortcut),
                  [this, step](const QVariantMap&) { nav_.setTrackHeight(nav_.trackHeight() + step.height); });
    }
    addAction(kToggleAutomationKeys.id, kToggleAutomationKeys.title, keys(kToggleAutomationKeys.shortcut),
              [this](const QVariantMap&) { nav_.setAutomationOpen(!nav_.automationOpen()); });
    addAction("automation.showLane", "Show automation lane", {}, [this](const QVariantMap& params) {
        QVariantMap lanes = nav_.automationLanes();
        lanes.insert(params.value("track").toString(), params.value("param"));
        nav_.setAutomationLanes(lanes);
        nav_.setAutomationOpen(true);
    });
}

void ViewerRegistry::addAction(const char* id, const char* title, QStringList shortcuts,
                               std::function<void(const QVariantMap&)> run) {
    shortcuts.removeAll(QString{});
    actions_.push_back({QString::fromUtf8(id), QString::fromUtf8(title), std::move(shortcuts), std::move(run)});
}

const ViewerRegistry::Action* ViewerRegistry::action(const QString& id) const {
    const auto found = std::ranges::find(actions_, id, &Action::id);
    return found == actions_.end() ? nullptr : &*found;
}

QVariantList ViewerRegistry::shortcuts() const {
    QVariantList list;
    for (const Action& entry : actions_) {
        for (const QString& shortcut : entry.shortcuts) {
            list.append(QVariantMap{{"id", entry.id}, {"shortcut", shortcut}});
        }
    }
    return list;
}

QVariantMap ViewerRegistry::actionInfo(const QString& id) const {
    const Action* found = action(id);
    return {{"title", found != nullptr ? found->title : QString{}},
            {"shortcut", found != nullptr ? found->shortcuts.value(0) : QString{}},
            {"enabled", found != nullptr},
            {"choices", automationModes()}};
}

void ViewerRegistry::activate(const QString& id) { runAction(id, {}); }

QVariant ViewerRegistry::runAction(const QString& id, const QVariantMap& params) {
    if (const Action* found = action(id)) {
        found->run(params);
    }
    return {};
}

void ViewerRegistry::stepToMarker(int step) {
    std::vector<core::Ticks> ats =
        project_.document().markers | std::views::transform(&model::Marker::at) | std::ranges::to<std::vector>();
    std::ranges::sort(ats);
    const core::Ticks now = project_.positionTicks();
    if (step > 0) {
        if (const auto next = std::ranges::upper_bound(ats, now); next != ats.end()) {
            seekToTicks(*next);
        }
    } else if (const auto later = std::ranges::lower_bound(ats, now); later != ats.begin()) {
        seekToTicks(*std::prev(later));
    }
}

void ViewerRegistry::seekToTicks(qint64 ticks) { emit seekRequested(project_.document().tempo.ticksToSeconds(ticks)); }

}  // namespace emerick::viewer
