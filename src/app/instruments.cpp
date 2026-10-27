#include "emerick/app/instruments.hpp"

#include <QGuiApplication>
#include <QObject>
#include <ranges>
#include <utility>

#include "emerick/app/registry.hpp"

namespace emerick::app {

namespace {

bool samePlugin(const model::PluginSlot& left, const model::PluginSlot& right) {
    return left.pluginId == right.pluginId && left.bundle == right.bundle;
}

}  // namespace

Instruments::~Instruments() {
    for (auto& entry : live_) {
        closeEditor(entry.second);
    }
}

void Instruments::sync(const model::Document& document) {
    std::map<std::string, Live> kept;
    for (const model::Track& track : document.tracks) {
        if (!track.instrument) {
            continue;
        }
        const model::PluginSlot& slot = *track.instrument;
        if (auto found = live_.find(track.id.value); found != live_.end() && samePlugin(found->second.slot, slot)) {
            kept.insert(live_.extract(found));
            continue;
        }
        Live live{.slot = slot, .instrument = nullptr, .error = {}, .window = nullptr};
        auto created = engine::ClapInstrument::create(slot.bundle, slot.pluginId);
        if (!created) {
            live.error = "Install " + slot.name + " or load another instrument; the track keeps its settings (" +
                         created.error() + ")";
        } else {
            live.instrument = *std::move(created);
            if (!slot.state.empty() && !live.instrument->loadState(slot.state)) {
                live.error = slot.name + " did not load its saved settings, so it plays with its defaults";
            }
        }
        kept.insert_or_assign(track.id.value, std::move(live));
    }
    for (auto& entry : live_) {
        closeEditor(entry.second);
    }
    live_ = std::move(kept);
    errors_ = live_ | std::views::filter([](const auto& entry) { return !entry.second.error.empty(); }) |
              std::views::transform([](const auto& entry) {
                  return engine::SessionError{.trackId = entry.first, .message = entry.second.error};
              }) |
              std::ranges::to<std::vector>();
}

void Instruments::attach(engine::Session& session) const {
    for (engine::Track& track : session.tracks) {
        track.instrument = find(model::TrackId{track.id});
    }
}

std::shared_ptr<engine::ClapInstrument> Instruments::find(const model::TrackId& id) const {
    const auto found = live_.find(id.value);
    return found == live_.end() ? nullptr : found->second.instrument;
}

bool Instruments::idle() {
    bool dirty = false;
    for (auto& [id, live] : live_) {
        if (!live.instrument) {
            continue;
        }
        dirty = live.instrument->idle() || dirty;
        if (const auto size = live.instrument->takeEditorResize(); size && live.window) {
            live.window->resize(static_cast<int>(size->width), static_cast<int>(size->height));
        }
    }
    return dirty;
}

model::Document Instruments::withStates(model::Document document) const {
    for (model::Track& track : document.tracks) {
        const auto instrument = find(track.id);
        if (!track.instrument || !instrument) {
            continue;
        }
        if (auto state = instrument->saveState()) {
            track.instrument->state = *std::move(state);
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
    engine::ClapInstrument& instrument = *live.instrument;
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
