#include "emerick/app/instruments.hpp"

#include <QGuiApplication>
#include <QObject>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::app {

namespace {

bool samePlugin(const model::PluginSlot& left, const model::PluginSlot& right) {
    return left.pluginId == right.pluginId && left.bundle == right.bundle && left.format == right.format;
}

double paramNumber(const engine::PluginParam& info, const ParamValue& given) {
    const auto number = [&](double value) {
        if (value < info.min || value > info.max) {
            throw AppError(info.name + std::format(" takes a value from {} to {}", info.min, info.max));
        }
        return info.stepped ? std::round(value) : value;
    };
    const auto choice = [&](const std::string& text) {
        return info.min + static_cast<double>(paramChoice(info, text));
    };
    return std::visit(core::Overloaded{number, choice}, given);
}

std::string lowered(std::string_view text) {
    return text | std::views::transform([](char letter) {
               return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
           }) |
           std::ranges::to<std::string>();
}

std::string missingMessage(const model::PluginSlot& slot, bool insert, const std::string& reason) {
    return "Install " + slot.name + (insert ? " or remove the insert" : " or load another instrument") + "; the " +
           (insert ? "insert" : "track") + " keeps its settings (" + reason + ")";
}

}  // namespace

std::optional<int> paramChannel(const engine::PluginParam& param) {
    constexpr std::string_view kPrefix = "Channel ";
    constexpr int kChannels = 16;
    if (!param.module.starts_with(kPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = std::string_view{param.module}.substr(kPrefix.size());
    int channel = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), channel);
    if (error != std::errc{} || end != digits.data() + digits.size() || channel < 1 || channel > kChannels) {
        return std::nullopt;
    }
    return channel;
}

std::size_t paramChoice(const engine::PluginParam& param, std::string_view text) {
    const std::vector<std::string_view> choices(param.choices.begin(), param.choices.end());
    return choiceIndex(param.name, choices, text, "run instrument.params for its choices");
}

std::optional<engine::PluginParam> paramNamed(const std::vector<engine::PluginParam>& params, std::string_view text) {
    if (const auto exact = std::ranges::find(params, text, &engine::PluginParam::name); exact != params.end()) {
        return *exact;
    }
    const std::string wanted = lowered(text);
    const auto folded =
        params | std::views::filter([&](const engine::PluginParam& param) { return lowered(param.name) == wanted; }) |
        std::ranges::to<std::vector>();
    if (folded.size() == 1) {
        return folded.front();
    }
    std::uint32_t id = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
    if (folded.empty() && error == std::errc{} && end == text.data() + text.size()) {
        if (const auto byId = std::ranges::find(params, id, &engine::PluginParam::id); byId != params.end()) {
            return *byId;
        }
    }
    return std::nullopt;
}

Instruments::~Instruments() {
    for (auto& entry : live_) {
        closeEditor(entry.second);
    }
}

void Instruments::keep(std::map<std::string, Live>& kept, const std::string& key, const model::TrackId& track,
                       const model::PluginSlot& slot, bool insert) {
    // An empty state means the plugin's defaults, which only a new instance has.
    if (auto found = live_.find(key); found != live_.end() && samePlugin(found->second.slot, slot) &&
                                      (found->second.slot.state == slot.state || !slot.state.empty())) {
        Live& live = found->second;
        if (live.instrument && live.slot.state != slot.state) {
            live.error = live.instrument->loadState(slot.state)
                             ? std::string{}
                             : slot.name + " did not load its saved settings, so it plays with its defaults";
        }
        live.slot = slot;
        kept.insert(live_.extract(found));
        return;
    }
    Live live{.slot = slot, .instrument = nullptr, .trackId = track.value, .error = {}, .window = nullptr};
    auto created = engine::createPlugin(slot.format, slot.bundle, slot.pluginId);
    if (!created) {
        live.error = missingMessage(slot, insert, created.error());
    } else {
        live.instrument = *std::move(created);
        if (!slot.state.empty() && !live.instrument->loadState(slot.state)) {
            live.error = slot.name + " did not load its saved settings, so it plays with its defaults";
        }
    }
    kept.insert_or_assign(key, std::move(live));
}

void Instruments::sync(const model::Document& document) {
    std::map<std::string, Live> kept;
    for (const model::Track& track : document.tracks) {
        if (track.instrument) {
            keep(kept, track.id.value, track.id, *track.instrument, false);
        }
        for (const model::Insert& insert : track.inserts) {
            if (insert.plugin) {
                keep(kept, insert.id.value, track.id, *insert.plugin, true);
            }
        }
    }
    for (auto& entry : live_) {
        closeEditor(entry.second);
    }
    live_ = std::move(kept);
    updateErrors();
}

// A plugin reports its latency once it is active, so we read it again on every idle.
void Instruments::updateErrors() {
    std::vector<engine::SessionError> errors;
    for (const Live& live : live_ | std::views::values) {
        if (!live.error.empty()) {
            errors.push_back({.trackId = live.trackId, .message = live.error});
        } else if (const core::Frames latency = live.instrument ? live.instrument->latency() : 0; latency > 0) {
            errors.push_back({.trackId = live.trackId,
                              .message = live.slot.name + " delays this track by " + std::to_string(latency) +
                                         " samples, and the other tracks play without that delay"});
        }
    }
    errors_ = std::move(errors);
}

void Instruments::attach(engine::Session& session, const model::Document& document) const {
    const PluginLookup inserts = insertPlugins();
    for (engine::Track& track : session.tracks) {
        track.instrument = find(model::TrackId{track.id});
        if (const model::Track* source = model::findTrack(document, model::TrackId{track.id})) {
            attachPluginInserts(track, *source, inserts);
        }
    }
}

std::shared_ptr<engine::Plugin> Instruments::plugin(const std::string& key) const {
    const auto found = live_.find(key);
    return found == live_.end() ? nullptr : found->second.instrument;
}

std::shared_ptr<engine::Plugin> Instruments::find(const model::TrackId& id) const { return plugin(id.value); }

std::shared_ptr<engine::Plugin> Instruments::find(const model::InsertId& id) const { return plugin(id.value); }

bool Instruments::idle() {
    bool dirty = false;
    for (auto& [id, live] : live_) {
        if (!live.instrument) {
            continue;
        }
        dirty = live.instrument->idle() || dirty;
        paramsChanged_ = live.instrument->takeParamsChanged() || paramsChanged_;
        if (const auto size = live.instrument->takeEditorResize(); size && live.window) {
            live.window->resize(static_cast<int>(size->width), static_cast<int>(size->height));
        }
    }
    updateErrors();
    return dirty;
}

model::Document Instruments::withStates(model::Document document) const {
    const auto save = [](model::PluginSlot& slot, const std::shared_ptr<engine::Plugin>& live) {
        if (!live) {
            return;
        }
        if (auto state = live->saveState()) {
            slot.state = *std::move(state);
        }
    };
    for (model::Track& track : document.tracks) {
        if (track.instrument) {
            save(*track.instrument, find(track.id));
        }
        for (model::Insert& insert : track.inserts) {
            if (insert.plugin) {
                save(*insert.plugin, find(insert.id));
            }
        }
    }
    return document;
}

void Instruments::showEditor(const model::TrackId& id, const std::string& title) {
    const auto found = live_.find(id.value);
    if (found == live_.end()) {
        throw AppError("the track has no instrument, run instrument.load first");
    }
    Live& live = found->second;
    if (!live.instrument) {
        throw AppError(live.error);
    }
    engine::Plugin& instrument = *live.instrument;
    if (!instrument.hasEditor()) {
        throw AppError(instrument.name() + " has no editor");
    }
    if (live.window) {
        live.window->show();
        live.window->raise();
        live.window->requestActivate();
        return;
    }
    if (instrument.openFloatingEditor(title)) {
        return;
    }
    auto window = std::make_unique<QWindow>();
    window->setTitle(qs(title));
    // A tool window floats over the app's windows, as plugin windows do in other DAWs.
    window->setFlag(Qt::Tool);
    window->setTransientParent(QGuiApplication::focusWindow());
    // Qt hands out the native view of a window as an integer, and the plugin embeds into that view.
    const auto size =
        instrument.embedEditor(reinterpret_cast<void*>(window->winId()));  // NOLINT(performance-no-int-to-ptr)
    if (!size) {
        throw AppError(instrument.name() + " cannot open its editor in a window");
    }
    window->resize(static_cast<int>(size->width), static_cast<int>(size->height));
    QObject::connect(window.get(), &QWindow::visibleChanged, window.get(), [this, key = id.value](bool visible) {
        if (const auto open = live_.find(key); !visible && open != live_.end() && open->second.window) {
            open->second.instrument->closeEditor();
            open->second.window.release()->deleteLater();
        }
    });
    window->show();
    live.window = std::move(window);
}

bool Instruments::editorOpen(const model::TrackId& id) const {
    const auto found = live_.find(id.value);
    return found != live_.end() && found->second.instrument && found->second.instrument->editorOpen();
}

Json Instruments::describe(const model::TrackId& id) const { return describePlugin(id.value); }

Json Instruments::describe(const model::InsertId& id) const { return describePlugin(id.value); }

Json Instruments::describePlugin(const std::string& key) const {
    const auto instrument = plugin(key);
    if (!instrument) {
        return nullptr;
    }
    return {{"editor", instrument->hasEditor()},
            {"latency", instrument->latency()},
            {"params", instrument->params() | std::views::transform([&](const engine::PluginParam& param) {
                           const double value = instrument->paramValue(param.id).value_or(param.defaultValue);
                           Json described{{"id", param.id},           {"name", param.name},
                                          {"module", param.module},   {"min", param.min},
                                          {"max", param.max},         {"default", param.defaultValue},
                                          {"stepped", param.stepped}, {"enum", param.enumerated},
                                          {"value", value},           {"text", instrument->paramText(param.id, value)},
                                          {"choices", param.choices}};
                           if (const auto channel = paramChannel(param)) {
                               described["channel"] = *channel;
                               described["description"] = std::format("Sets what MIDI channel {} plays", *channel);
                           }
                           return described;
                       }) | std::ranges::to<Json::array_t>()}};
}

ParamSet Instruments::setParam(const model::TrackId& id, std::uint32_t param, const ParamValue& given) {
    return setPluginParam(id.value, param, given);
}

ParamSet Instruments::setParam(const model::InsertId& id, std::uint32_t param, const ParamValue& given) {
    return setPluginParam(id.value, param, given);
}

ParamSet Instruments::setPluginParam(const std::string& key, std::uint32_t param, const ParamValue& given) {
    const auto found = live_.find(key);
    if (found == live_.end() || !found->second.instrument) {
        throw AppError(found == live_.end() ? "the track has no instrument, run instrument.load first"
                                            : found->second.error);
    }
    Live& live = found->second;
    const std::vector<engine::PluginParam> params = live.instrument->params();
    const auto info = std::ranges::find(params, param, &engine::PluginParam::id);
    if (info == params.end()) {
        throw AppError(live.slot.name + " has no param " + std::to_string(param) + ", list its params for ids");
    }
    const double value = paramNumber(*info, given);
    live.instrument->setParam(param, value);
    live.slot.state = live.instrument->saveState().value_or(live.slot.state);
    return {.state = live.slot.state, .value = value, .text = live.instrument->paramText(param, value)};
}

void Instruments::closeEditor(Live& live) {
    if (live.window) {
        live.window->disconnect();
    }
    if (live.instrument) {
        live.instrument->closeEditor();
    }
    live.window.reset();
}

}  // namespace emerick::app
