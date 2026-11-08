#include "emerick/broadcast/plugin.hpp"

#include <QQmlEngine>
#include <QtEnvironmentVariables>
#include <array>
#include <ranges>
#include <string_view>
#include <utility>

#include "broadcaster.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/params.hpp"
#include "emerick/app/timeline_video.hpp"
#include "profile.hpp"

namespace emerick::broadcast {

namespace {

constexpr const char* kProfilesKey = "broadcast.profiles";
constexpr const char* kTapOwner = "broadcast:";
constexpr std::array<std::string_view, 5> kStateNames{"idle", "connecting", "live", "retrying", "error"};
constexpr auto kRefreshInterval = std::chrono::milliseconds{500};

std::string_view stateName(State state) { return kStateNames.at(static_cast<std::size_t>(state)); }

app::Json statusJson(const Status& status) {
    const auto uptime =
        status.liveSince ? std::chrono::steady_clock::now() - *status.liveSince : std::chrono::steady_clock::duration{};
    return {{"state", stateName(status.state)},
            {"uptimeSeconds", std::chrono::duration_cast<std::chrono::seconds>(uptime).count()},
            {"bytesSent", status.bytesSent},
            {"droppedFrames", status.droppedFrames},
            {"retryInSeconds", status.retryIn.count()},
            {"lastError", status.lastError}};
}

std::optional<std::size_t> profileIndex(const app::Json& profiles, const std::string& name) {
    const auto found = std::ranges::find_if(profiles, [&](const app::Json& stored) { return stored["name"] == name; });
    return found == profiles.end() ? std::nullopt : std::optional{static_cast<std::size_t>(found - profiles.begin())};
}

}  // namespace

Broadcasts::Broadcasts(app::Registry& registry, app::ProjectController& controller)
    : registry_(registry), controller_(controller) {
    refresh_.setInterval(kRefreshInterval);
    connect(&refresh_, &QTimer::timeout, this, &Broadcasts::changed);
    connect(&controller_, &app::ProjectController::fileChanged, this, [this] {
        for (const auto& broadcaster : running_ | std::views::values) {
            broadcaster->setTitle(controller_.name().toStdString());
        }
    });
    followSettings();
}

// NOLINTNEXTLINE(bugprone-exception-escape): a broadcast that cannot stop while we quit ends the process.
Broadcasts::~Broadcasts() { stopAll(); }

bool Broadcasts::enabled() const { return registry_.settingValue(kEnabledSetting).get<bool>(); }

app::Json Broadcasts::storedProfiles() const {
    const app::Json stored = registry_.privateValue(kProfilesKey);
    return stored.is_array() ? stored : app::Json::array();
}

app::Json Broadcasts::listJson() const {
    app::Json broadcasts = app::Json::array();
    for (const app::Json& stored : storedProfiles()) {
        const Profile profile = profileFromStore(stored);
        const auto running = running_.find(profile.name);
        app::Json entry = maskedProfile(profile);
        entry.update(statusJson(running != running_.end() ? running->second->status() : Status{}));
        broadcasts.push_back(std::move(entry));
    }
    return {{"enabled", enabled()}, {"broadcasts", std::move(broadcasts)}};
}

QVariantList Broadcasts::list() const { return app::toVariant(listJson()["broadcasts"]).toList(); }

void Broadcasts::setPanelOpen(bool open) {
    if (std::exchange(panelOpen_, open) != open) {
        emit changed();
    }
}

QString Broadcasts::badge() const {
    const auto anyIn = [this](State state) {
        return std::ranges::any_of(running_ | std::views::values,
                                   [&](const auto& broadcaster) { return broadcaster->status().state == state; });
    };
    if (anyIn(State::live)) {
        return "live";
    }
    return anyIn(State::retrying) || anyIn(State::connecting) ? "retrying" : "";
}

app::Json Broadcasts::save(const app::Json& params) {
    const auto name = app::param<std::string>(params, "name");
    if (running_.contains(name)) {
        throw app::AppError("stop broadcast " + name + " before you edit it");
    }
    app::Json profiles = storedProfiles();
    const auto index = profileIndex(profiles, name);
    const Profile profile = parseProfile(params, index ? profileFromStore(profiles[*index]) : Profile{});
    if (index) {
        profiles[*index] = storedProfile(profile);
    } else {
        profiles.push_back(storedProfile(profile));
    }
    registry_.setPrivateValue(kProfilesKey, profiles);
    emit changed();
    return maskedProfile(profile);
}

app::Json Broadcasts::remove(const std::string& name) {
    app::Json profiles = storedProfiles();
    const auto index = profileIndex(profiles, name);
    if (!index) {
        throw app::AppError("no broadcast " + name + "; run broadcast.list for their names");
    }
    stop(name);
    profiles.erase(*index);
    registry_.setPrivateValue(kProfilesKey, profiles);
    emit changed();
    return {{"removed", name}};
}

app::Json Broadcasts::start(const std::string& name) {
    const app::Json profiles = storedProfiles();
    const auto index = profileIndex(profiles, name);
    if (!index) {
        throw app::AppError("no broadcast " + name + "; save one with broadcast.save first");
    }
    if (registry_.headless() && qEnvironmentVariable("EMERICK_BROADCAST") != QStringLiteral("1")) {
        throw app::AppError("a headless run broadcasts only with EMERICK_BROADCAST=1");
    }
    if (!running_.contains(name)) {
        const std::filesystem::path ffmpeg = ffmpeg_ ? *ffmpeg_ : app::findFfmpeg();
        auto tap = std::make_shared<engine::SignalTap>();
        tap->stereo = true;
        tap->holdsStream = true;
        controller_.setTap(kTapOwner + name, tap);
        running_[name] = std::make_unique<Broadcaster>(profileFromStore(profiles[*index]), ffmpeg,
                                                       controller_.engine().layout().sampleRate, std::move(tap),
                                                       controller_.name().toStdString());
        refresh_.start();
        emit changed();
    }
    app::Json result = maskedProfile(profileFromStore(profiles[*index]));
    result.update(statusJson(running_.at(name)->status()));
    return result;
}

app::Json Broadcasts::stop(const std::string& name) {
    if (running_.erase(name) > 0) {
        controller_.setTap(kTapOwner + name, nullptr);
        if (running_.empty()) {
            refresh_.stop();
        }
        emit changed();
    }
    return {{"stopped", name}};
}

void Broadcasts::stopAll() {
    while (!running_.empty()) {
        stop(running_.begin()->first);
    }
}

void Broadcasts::followSettings() {
    wasEnabled_ = enabled();
    connect(&registry_, &app::Registry::settingsChanged, this, [this] {
        const bool now = enabled();
        if (std::exchange(wasEnabled_, now) == now) {
            return;
        }
        if (!now) {
            stopAll();
            setPanelOpen(false);
        }
        emit registry_.menusChanged();
        emit changed();
    });
}

namespace {

void requireEnabled(const Broadcasts& broadcasts) {
    if (!broadcasts.enabled()) {
        throw app::AppError("turn on broadcasting in Settings / Plugins first");
    }
}

app::Json nameSchema() { return {{"required", {"name"}}, {"properties", {{"name", {{"type", "string"}}}}}}; }

app::Json saveSchema() {
    const app::Json text{{"type", "string"}};
    return {{"required", {"name"}},
            {"properties",
             {{"name", text},
              {"host", text},
              {"port", {{"type", "integer"}}},
              {"tls", {{"type", "boolean"}}},
              {"mount", text},
              {"user", text},
              {"password", text},
              {"format", {{"type", "string"}, {"enum", {"opus", "mp3"}}}},
              {"bitrate", {{"type", "integer"}}},
              {"streamName", text},
              {"genre", text},
              {"description", text}}}};
}

void registerActions(app::Registry& registry, Broadcasts& broadcasts) {
    const auto shown = [&broadcasts] { return broadcasts.enabled(); };
    const auto named = [&broadcasts](app::Json (Broadcasts::*act)(const std::string&)) {
        return [&broadcasts, act](const app::Json& params) {
            requireEnabled(broadcasts);
            return (broadcasts.*act)(app::param<std::string>(params, "name"));
        };
    };
    registry.addAction(
        {.id = "broadcast.show",
         .title = "Show broadcasts",
         .description = "Opens the panel that starts, stops and edits the broadcasts to Icecast servers.",
         .aliases = {"icecast", "stream live", "radio"},
         .category = "Broadcast",
         .shown = shown,
         .run =
             [&broadcasts](const app::Json&) {
                 requireEnabled(broadcasts);
                 broadcasts.setPanelOpen(true);
                 return app::Json::object();
             },
         .menus = {app::kTransportMenu},
         .windowOnly = true});
    registry.addAction({.id = "broadcast.save",
                        .title = "Save broadcast",
                        .description = "Adds a broadcast profile, or changes the one with this name. An empty or "
                                       "missing password keeps the saved one.",
                        .category = "Broadcast",
                        .shown = shown,
                        .schema = saveSchema(),
                        .run = [&broadcasts](const app::Json& params) {
                            requireEnabled(broadcasts);
                            return broadcasts.save(params);
                        }});
    registry.addAction({.id = "broadcast.remove",
                        .title = "Remove broadcast",
                        .category = "Broadcast",
                        .shown = shown,
                        .schema = nameSchema(),
                        .run = named(&Broadcasts::remove)});
    registry.addAction({.id = "broadcast.start",
                        .title = "Start broadcast",
                        .description = "Streams the master to the profile's Icecast server until it stops, retrying "
                                       "when the connection drops.",
                        .category = "Broadcast",
                        .shown = shown,
                        .schema = nameSchema(),
                        .run = named(&Broadcasts::start)});
    registry.addAction({.id = "broadcast.stop",
                        .title = "Stop broadcast",
                        .category = "Broadcast",
                        .shown = shown,
                        .schema = nameSchema(),
                        .run = named(&Broadcasts::stop)});
    registry.addAction({.id = "broadcast.list",
                        .title = "List broadcasts",
                        .description = "Lists the broadcast profiles with passwords masked, and each one's state, "
                                       "uptime, bytes sent, dropped frames and last error.",
                        .category = "Broadcast",
                        .shown = shown,
                        .run = [&broadcasts](const app::Json&) { return broadcasts.listJson(); }});
    registry.addStateProvider("broadcast", [&broadcasts] { return broadcasts.listJson(); });
}

}  // namespace

std::unique_ptr<Broadcasts> registerWith(app::Registry& registry, app::ProjectController& controller, app::Nav& nav) {
    app::addSettingsSection(registry, nav, {.key = "broadcast", .title = "Broadcast", .group = app::kPluginsGroup});
    registry.addSetting({.id = kEnabledSetting,
                         .title = "Enable broadcasting",
                         .aliases = {"icecast", "stream live"},
                         .category = "Settings / Broadcast",
                         .type = app::SettingType::Boolean,
                         .defaultValue = false,
                         .hint = "Turn on to stream the master to Icecast servers from Transport / Show broadcasts",
                         .location = "settings.broadcast"});
    auto broadcasts = std::make_unique<Broadcasts>(registry, controller);
    registerActions(registry, *broadcasts);
    registry.addOverlay("qrc:/qt/qml/Emerick/Broadcast/BroadcastPanel.qml");
    registry.addTransportBadge("qrc:/qt/qml/Emerick/Broadcast/LiveBadge.qml");
    qmlRegisterSingletonInstance("Emerick.Broadcast", 1, 0, "Broadcasts", broadcasts.get());
    return broadcasts;
}

}  // namespace emerick::broadcast
