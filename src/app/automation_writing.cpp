#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

#include "emerick/app/automation_params.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/model/automation.hpp"

namespace emerick::app {

namespace {

// A written value becomes a point once it moves this share of the parameter's range, or after kWriteInterval.
constexpr double kWriteStep = 0.01;
constexpr double kWriteInterval = 0.05;
// Touch ramps back to the lane over this long after the control is let go, as Logic's ramp time does.
constexpr double kTouchReturn = 0.2;

bool writes(model::AutomationMode mode) {
    return mode == model::AutomationMode::touch || mode == model::AutomationMode::latch;
}

void writeIntoLane(model::Track& track, const std::string& param, std::span<const model::AutomationPoint> points,
                   std::pair<core::Ticks, core::Ticks> span, core::Ticks returnAt, double lastValue) {
    model::AutomationLane* lane = model::findLane(track, param);
    if (lane == nullptr) {
        track.automation.push_back({.param = param, .points = {}});
        lane = &track.automation.back();
    }
    const auto [start, end] = span;
    const std::optional<double> before = model::automationValueAt(*lane, start);
    const std::optional<double> after = returnAt > end ? model::automationValueAt(*lane, returnAt) : std::nullopt;
    const core::Ticks erasedTo = after ? returnAt : end;
    std::erase_if(lane->points,
                  [&](const model::AutomationPoint& point) { return point.tick >= start && point.tick <= erasedTo; });
    if (before) {
        lane->points.push_back({.tick = start, .value = *before});
    }
    lane->points.insert(lane->points.end(), points.begin(), points.end());
    lane->points.push_back({.tick = end, .value = lastValue});
    if (after) {
        lane->points.push_back({.tick = returnAt, .value = *after});
    }
    model::sortPoints(*lane);
}

}  // namespace

bool ProjectController::writeAutomation(const model::TrackId& id, const std::string& param, double value) {
    const model::Track* track = project_.track(id);
    if (track == nullptr || !engine_.playing() || !writes(track->automationMode)) {
        return false;
    }
    const auto resolved = automationParam(registry_, project_.document(), *track, instruments_.insertPlugins(), param);
    if (!resolved) {
        return false;
    }
    const core::Ticks tick = engine_.position().ticks;
    auto pass = std::ranges::find_if(
        passes_, [&](const AutomationPass& each) { return each.track == id && each.param == param; });
    const bool started = pass == passes_.end();
    if (started) {
        passes_.push_back({.track = id, .param = param, .start = tick, .points = {}, .lastValue = value});
        pass = std::prev(passes_.end());
    }
    pass->lastValue = value;
    const core::TempoMap& tempo = project_.document().tempo;
    const bool due = pass->points.empty() || [&] {
        const model::AutomationPoint& last = pass->points.back();
        const bool moved = std::abs(value - last.value) >= (resolved->maximum - resolved->minimum) * kWriteStep;
        const bool waited = tempo.ticksToSeconds(tick) - tempo.ticksToSeconds(last.tick) >= kWriteInterval;
        return moved || (waited && value != last.value);
    }();
    if (due) {
        pass->points.push_back({.tick = tick, .value = value});
    }
    if (started || resolved->target == engine::AutomationTarget::send) {
        scheduleFlush();
    }
    return true;
}

std::optional<double> ProjectController::automatedValue(const model::Track& track, std::string_view param) const {
    const auto pass = std::ranges::find_if(
        passes_, [&](const AutomationPass& each) { return each.track == track.id && each.param == param; });
    if (pass != passes_.end()) {
        return pass->lastValue;
    }
    const model::AutomationLane* lane = model::findLane(track, param);
    if (lane == nullptr || track.automationMode == model::AutomationMode::off) {
        return std::nullopt;
    }
    return model::automationValueAt(*lane, engine_.position().ticks);
}

void ProjectController::releaseAutomation() {
    std::vector<AutomationPass> touched;
    std::erase_if(passes_, [&](AutomationPass& pass) {
        const model::Track* track = project_.track(pass.track);
        if (track != nullptr && track->automationMode == model::AutomationMode::latch) {
            return false;
        }
        touched.push_back(std::move(pass));
        return true;
    });
    commitPasses(std::move(touched), engine_.position().ticks);
}

void ProjectController::commitPasses(std::vector<AutomationPass> passes, core::Ticks end) {
    if (passes.empty()) {
        return;
    }
    const core::TempoMap& tempo = project_.document().tempo;
    project_.group("write automation", [&] {
        for (const AutomationPass& pass : passes) {
            const model::Track* track = project_.track(pass.track);
            if (track == nullptr) {
                continue;
            }
            const bool touch = track->automationMode == model::AutomationMode::touch;
            const core::Ticks stop = std::max(end, pass.start);
            const core::Ticks returnAt = touch ? tempo.secondsToTicks(tempo.ticksToSeconds(stop) + kTouchReturn) : stop;
            project_.editTrack(pass.track, "write automation", [&](model::Track& edited) {
                writeIntoLane(edited, pass.param, pass.points, {pass.start, stop}, returnAt, pass.lastValue);
            });
        }
    });
    scheduleFlush();
}

void ProjectController::applyPasses(engine::Session& session) const {
    for (const AutomationPass& pass : passes_) {
        const model::Track* track = project_.track(pass.track);
        const auto engineTrack = std::ranges::find(session.tracks, pass.track.value, &engine::Track::id);
        const auto param = track == nullptr ? std::nullopt
                                            : automationParam(registry_, project_.document(), *track,
                                                              instruments_.insertPlugins(), pass.param);
        if (!param || engineTrack == session.tracks.end()) {
            continue;
        }
        std::erase_if(engineTrack->automation, [&](const engine::AutomationLane& lane) {
            return lane.target == param->target && lane.bus == param->bus && lane.insert == param->insert &&
                   lane.param == param->insertParam;
        });
        switch (param->target) {
            case engine::AutomationTarget::gain:
                engineTrack->gain = gainFromDb(pass.lastValue);
                break;
            case engine::AutomationTarget::pan:
                engineTrack->pan = static_cast<float>(pass.lastValue);
                break;
            case engine::AutomationTarget::mute:
                engineTrack->mute = pass.lastValue >= 0.5;
                break;
            case engine::AutomationTarget::send:
                for (engine::Send& send : engineTrack->sends) {
                    if (send.bus == param->bus) {
                        send.gain = gainFromDb(pass.lastValue);
                    }
                }
                break;
            case engine::AutomationTarget::insert:
                for (engine::Insert& insert : engineTrack->inserts) {
                    if (insert.id == param->insert && !insert.plugin) {
                        insert.params.at(param->insertParam) = static_cast<float>(pass.lastValue);
                    }
                }
                break;
        }
    }
}

}  // namespace emerick::app
