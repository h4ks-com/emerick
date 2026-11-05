#include "viewer_registry.hpp"

#include <QStringList>
#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>

#include "emerick/app/project_view.hpp"
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

std::optional<model::ClipRef> firstMidiClip(const model::Document& document) {
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            if (std::holds_alternative<model::MidiClip>(clip)) {
                return model::ClipRef{.track = &track, .clip = &clip};
            }
        }
    }
    return std::nullopt;
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
    using app::kEditClipKeys, app::kToggleMixerKeys, app::kTogglePianoRollKeys;
    addAction(kToggleMixerKeys.id, kToggleMixerKeys.title, keys(kToggleMixerKeys.shortcut),
              [this](const QVariantMap&) { nav_.setMixerOpen(!nav_.mixerOpen()); });
    addAction(kTogglePianoRollKeys.id, kTogglePianoRollKeys.title,
              keys(kTogglePianoRollKeys.shortcut, kTogglePianoRollKeys.extraShortcut), [this](const QVariantMap&) {
                  if (nav_.pianoRollOpen()) {
                      nav_.setPianoRollOpen(false);
                  } else {
                      openInPianoRoll(nav_.pianoRollClip());
                  }
              });
    addAction(kEditClipKeys.id, kEditClipKeys.title, {},
              [this](const QVariantMap& params) { openInPianoRoll(params.value("clip").toString()); });
    addAction("automation.showLane", "Show automation lane", {}, [this](const QVariantMap& params) {
        nav_.showLane(params.value("track").toString(), params.value("param").toString(), params.value("add").toBool(),
                      params.value("replace").toString());
        nav_.setAutomationOpen(true);
    });
    addAction("automation.hideLane", "Hide automation lane", {}, [this](const QVariantMap& params) {
        nav_.hideLane(params.value("track").toString(), params.value("param").toString());
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

// The piano roll shows the velocity lane and every controller lane the clip holds data on. Without a clip it opens on
// the project's first MIDI clip.
void ViewerRegistry::openInPianoRoll(const QString& clip) {
    const model::Document& document = project_.document();
    const auto found =
        model::findClip(document, model::ClipId{clip.toStdString()}).or_else([&] { return firstMidiClip(document); });
    const auto* midi = found ? std::get_if<model::MidiClip>(found->clip) : nullptr;
    if (midi == nullptr) {
        return;
    }
    QStringList lanes{QString::fromUtf8(app::kVelocityLane.data(), static_cast<qsizetype>(app::kVelocityLane.size()))};
    for (const app::Json& choice : app::laneChoices(*midi)) {
        const QString lane = QString::fromStdString(choice.at("id").get<std::string>());
        if (choice.at("hasData").get<bool>() && !lanes.contains(lane)) {
            lanes.append(lane);
        }
    }
    settings_.insert("midi.lanes", lanes);
    emit settingsChanged();
    nav_.setPianoRollClip(QString::fromStdString(midi->id.value));
    nav_.setPianoRollOpen(true);
}

void ViewerRegistry::seekToTicks(qint64 ticks) { emit seekRequested(project_.document().tempo.ticksToSeconds(ticks)); }

}  // namespace emerick::viewer
