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

constexpr int kQtLargestWindow = (1 << 24) - 1;

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
    // An empty state means the plugin's defaults, which only a new instance has, unless we kept them as it started, as
    // we do for an insert, so undoing its first edit keeps its window open.
    if (auto found = live_.find(key);
        found != live_.end() && samePlugin(found->second.slot, slot) &&
        (found->second.slot.state == slot.state || !slot.state.empty() || !found->second.defaults.empty())) {
        Live& live = found->second;
        if (live.instrument && live.slot.state != slot.state) {
            live.error = live.instrument->loadState(slot.state.empty() ? live.defaults : slot.state)
                             ? std::string{}
                             : slot.name + " did not load its saved settings, so it plays with its defaults";
        }
        live.slot = slot;
        kept.insert(live_.extract(found));
        return;
    }
    Live live{.slot = slot,
              .instrument = nullptr,
              .trackId = track.value,
              .error = {},
              .window = nullptr,
              .insert = insert,
              .defaults = {}};
    auto created = engine::createPlugin(slot.format, slot.bundle, slot.pluginId);
    if (!created) {
        live.error = missingMessage(slot, insert, created.error());
    } else {
        live.instrument = *std::move(created);
        if (insert) {
            live.defaults = live.instrument->saveState().value_or(std::vector<std::uint8_t>{});
        }
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

void Instruments::updateErrors() {
    errors_ = live_ | std::views::values | std::views::filter([](const Live& live) { return !live.error.empty(); }) |
              std::views::transform([](const Live& live) {
                  return engine::SessionError{.trackId = live.trackId, .message = live.error};
              }) |
              std::ranges::to<std::vector>();
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
        const bool changed = live.instrument->idle();
        if (changed) {
            collectPluginEdit(live, id);
        }
        dirty = changed || dirty;
        paramsChanged_ = live.instrument->takeParamsChanged() || paramsChanged_;
        if (const auto size = live.instrument->takeEditorResize(); size && live.window) {
            fitWindow(live, *size);
        }
    }
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

void Instruments::collectPluginEdit(Live& live, const std::string& key) {
    auto state = live.instrument->saveState();
    if (!state || *state == live.slot.state) {
        return;
    }
    live.slot.state = *state;
    pluginEdits_.push_back({.track = model::TrackId{live.trackId},
                            .insert = live.insert ? std::optional{model::InsertId{key}} : std::nullopt,
                            .name = live.slot.name,
                            .state = *std::move(state)});
}

void Instruments::fitWindow(Live& live, engine::EditorSize size) {
    const QSize fitted{static_cast<int>(size.width), static_cast<int>(size.height)};
    if (live.instrument->editorResizable()) {
        live.window->setMinimumSize({1, 1});
        live.window->setMaximumSize({kQtLargestWindow, kQtLargestWindow});
    } else {
        live.window->setMinimumSize(fitted);
        live.window->setMaximumSize(fitted);
    }
    live.window->resize(fitted);
}

void Instruments::showEditor(const model::TrackId& id, const std::string& title) {
    showPluginEditor(id.value, title, "the track has no instrument, run instrument.load first");
}

void Instruments::showEditor(const model::InsertId& id, const std::string& title) {
    showPluginEditor(id.value, title, "no plugin insert " + id.value + ", read the track's inserts from state.get");
}

void Instruments::closeEditor(const model::InsertId& id) {
    if (const auto found = live_.find(id.value); found != live_.end()) {
        closeEditor(found->second);
    }
}

void Instruments::showPluginEditor(const std::string& key, const std::string& title, const std::string& missing) {
    const auto found = live_.find(key);
    if (found == live_.end()) {
        throw AppError(missing);
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
        throw AppError(instrument.name() + " cannot open its editor in a window; it keeps playing without one");
    }
    live.window = std::move(window);
    fitWindow(live, *size);
    QWindow* shown = live.window.get();
    QObject::connect(shown, &QWindow::visibleChanged, shown, [this, shown](bool visible) {
        if (Live* open = withWindow(shown); !visible && open != nullptr) {
            open->instrument->closeEditor();
            open->window.release()->deleteLater();
        }
    });
    QObject::connect(shown, &QWindow::widthChanged, shown, [this, shown] { followWindow(shown); });
    QObject::connect(shown, &QWindow::heightChanged, shown, [this, shown] { followWindow(shown); });
    shown->show();
}

Instruments::Live* Instruments::withWindow(const QWindow* window) {
    const auto found = std::ranges::find_if(live_, [window](const auto& entry) {
        return entry.second.window != nullptr && entry.second.window.get() == window;
    });
    return found == live_.end() ? nullptr : &found->second;
}

// The user resized the window, so the editor takes the size or the nearest one it can.
void Instruments::followWindow(QWindow* window) {
    Live* open = withWindow(window);
    if (open == nullptr || !open->instrument->editorResizable()) {
        return;
    }
    const engine::EditorSize taken = open->instrument->resizeEditor(
        {.width = static_cast<std::uint32_t>(window->width()), .height = static_cast<std::uint32_t>(window->height())});
    if (std::cmp_not_equal(taken.width, window->width()) || std::cmp_not_equal(taken.height, window->height())) {
        window->resize(static_cast<int>(taken.width), static_cast<int>(taken.height));
    }
}

bool Instruments::editorOpen(const model::TrackId& id) const { return pluginEditorOpen(id.value); }

bool Instruments::editorOpen(const model::InsertId& id) const { return pluginEditorOpen(id.value); }

bool Instruments::pluginEditorOpen(const std::string& key) const {
    const auto found = live_.find(key);
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
