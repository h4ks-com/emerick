#include "emerick/app/core_actions.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "emerick/app/action_params.hpp"
#include "emerick/app/view_actions.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxPort = 65535;
constexpr int kMaxRecordingDelay = 8192;

Json revealChain(Registry& registry, const std::string& id) { return Json{{"chain", registry.reveal(id).chain}}; }

struct PaneWidth {
    const char* id;
    const char* title;
    const char* alias;
    int defaultWidth;
    int minimum;
    int maximum;
};

constexpr std::array kPaneWidths{
    PaneWidth{"view.inspectorWidth", "Inspector width", "sidebar width", 260, 200, 640},
    PaneWidth{"view.headerWidth", "Track header width", "piano keyboard width", 240, 180, 480},
    PaneWidth{"view.stripWidth", "Mixer strip width", "channel strip width", 84, 72, 240},
};

void registerPaneWidths(Registry& registry) {
    for (const PaneWidth& pane : kPaneWidths) {
        registry.addSetting({.id = pane.id,
                             .title = pane.title,
                             .aliases = {pane.alias},
                             .category = "Settings / View",
                             .type = SettingType::Integer,
                             .defaultValue = pane.defaultWidth,
                             .minimum = pane.minimum,
                             .maximum = pane.maximum,
                             .hint = "Drag the pane's edge to resize it",
                             .location = "settings.general",
                             .merge = [pane](const Json&, const Json& change) {
                                 return change.is_number() ? Json(std::clamp(static_cast<int>(change.get<double>()),
                                                                             pane.minimum, pane.maximum))
                                                           : change;
                             }});
    }
}

void registerViewActions(Registry& registry, Nav& nav) {
    registerPaneWidths(registry);
    for (const HeightStep& step : kHeightSteps) {
        registry.addAction({.id = step.id,
                            .title = step.title,
                            .aliases = {step.alias},
                            .category = "View",
                            .shortcut = step.shortcut,
                            .run =
                                [&nav, step](const Json&) {
                                    nav.setTrackHeight(nav.trackHeight() + step.height);
                                    return Json{{"trackHeight", nav.trackHeight()}};
                                },
                            .menus = {kViewMenu}});
    }
    registry.addAction({.id = "view.toggleInspector",
                        .title = "Show or hide inspector",
                        .aliases = {"track inspector", "side panel"},
                        .category = "View",
                        .shortcut = "I",
                        .run =
                            [&nav](const Json&) {
                                nav.setInspectorOpen(!nav.inspectorOpen());
                                return Json{{"inspectorOpen", nav.inspectorOpen()}};
                            },
                        .menus = {kViewMenu},
                        .windowOnly = true});
    registry.addAction({.id = "view.toggleMixer",
                        .title = "Show or hide mixer",
                        .aliases = {"mixer", "mixing console", "channel strips"},
                        .category = "View",
                        .shortcut = "X",
                        .run =
                            [&nav](const Json&) {
                                nav.setMixerOpen(!nav.mixerOpen());
                                return Json{{"mixerOpen", nav.mixerOpen()}};
                            },
                        .menus = {kViewMenu},
                        .windowOnly = true});
}

}  // namespace

const char* appVersion() { return EMERICK_VERSION; }

void addSettingsSection(Registry& registry, Nav& nav, const SettingsSection& section) {
    // Copies of the handlers share the key, so copying a handler never allocates.
    auto key = std::make_shared<const std::string>(section.key);
    registry.addLocation({.id = "settings." + section.key,
                          .parent = "settings",
                          .title = section.title,
                          .group = section.group,
                          .reveal = [&nav, key] { nav.setSettingsTab(qs(*key)); }});
    registry.addAction({.id = "settings.show." + section.key,
                        .title = "Show " + section.title + (section.menus.empty() ? " settings" : ""),
                        .aliases = {section.key + " preferences", section.title + " settings"},
                        .category = "Settings",
                        .shortcut = section.shortcut,
                        .run = [&registry, key](const Json&) { return revealChain(registry, "settings." + *key); },
                        .menus = section.menus});
}

std::function<Json(const Json&, const Json&)> readablePath(const Registry& registry) {
    return [&registry](const Json&, const Json& change) {
        if (!change.is_string() || change.get<std::string>().empty()) {
            return change;
        }
        return Json(registry.checkedPath(change.get<std::string>(), PathAccess::Read).string());
    };
}

std::function<Json(const Json&, const Json&)> readableFolders(const Registry& registry) {
    return [&registry](const Json&, const Json& change) {
        if (!change.is_array()) {
            return change;
        }
        Json checked = Json::array();
        for (const Json& folder : change) {
            checked.push_back(folder.is_string()
                                  ? Json(registry.checkedPath(folder.get<std::string>(), PathAccess::Read).string())
                                  : folder);
        }
        return checked;
    };
}

void addPathSetting(Registry& registry, Setting setting, FileDialogMode mode, const std::string& nameFilter) {
    auto id = std::make_shared<const std::string>(setting.id);
    setting.merge = readablePath(registry);
    setting.action = *id + ".choose";
    registry.addAction({.id = setting.action,
                        .title = "Choose " + setting.title,
                        .category = setting.category,
                        .schema = {{"required", {"path"}}, {"properties", {{"path", {{"type", "string"}}}}}},
                        .run =
                            [&registry, id](const Json& params) {
                                registry.setSetting(*id, param<std::string>(params, "path"));
                                return Json{{"path", registry.settingValue(*id)}};
                            },
                        .dialog = FileDialog{.param = "path", .mode = mode, .nameFilter = nameFilter}});
    registry.addSetting(std::move(setting));
}

void registerCoreActions(Registry& registry, Nav& nav, const Backends& backends) {
    registry.addStateProvider("nav", [&nav] { return nav.toJson(); });
    // Copies of the provider share these names, so copying the provider never allocates.
    auto names = std::make_shared<const Backends>(backends);
    registry.addStateProvider(
        "app", [names] { return Json{{"version", appVersion()}, {"audio", names->audio}, {"midi", names->midi}}; });
    registry.addStateProvider("limits", [&registry] { return registry.limitsState(); });

    registry.addLocation({.id = "palette", .reveal = [&nav] { nav.setPaletteOpen(true); }});
    registry.addLocation({.id = "about", .reveal = [&nav] { nav.setAboutOpen(true); }});
    registry.addLocation({.id = "settings", .reveal = [&nav] {
                              nav.setPaletteOpen(false);
                              nav.setPage(Page::Settings);
                          }});
    addSettingsSection(registry, nav, {.key = "general", .title = "General"});
    addSettingsSection(registry, nav, {.key = "audio", .title = "Audio"});
    addSettingsSection(registry, nav, {.key = "midi", .title = "MIDI"});
    addSettingsSection(registry, nav,
                       {.key = "keys", .title = "Key Commands", .shortcut = "Alt+K", .menus = {kSettingsMenu}});
    registry.addAction({.id = "keys.setShortcut",
                        .title = "Set action shortcut",
                        .aliases = {"rebind key", "key binding", "assign shortcut"},
                        .category = "Settings",
                        .schema = {{"required", {"id", "shortcut"}},
                                   {"properties", {{"id", {{"type", "string"}}}, {"shortcut", {{"type", "string"}}}}}},
                        .run = [&registry](const Json& params) {
                            return registry.setShortcut(param<std::string>(params, "id"),
                                                        param<std::string>(params, "shortcut"));
                        }});

    registry.addAction({.id = "palette.open",
                        .title = "Search",
                        .aliases = {"command palette", "search actions", "open command palette"},
                        .category = "General",
                        .shortcut = "Ctrl+K",
                        .run = [&registry](const Json&) { return revealChain(registry, "palette"); },
                        .menus = {kHelpMenu}});
    registry.addAction({.id = "settings.open",
                        .title = "Open settings",
                        .aliases = {"preferences", "options"},
                        .category = "General",
                        .shortcut = "Ctrl+,",
                        .run = [&registry](const Json&) { return revealChain(registry, "settings"); },
                        .menus = {kSettingsMenu}});
    registry.addAction({.id = "settings.close",
                        .title = "Close settings",
                        .aliases = {"back"},
                        .category = "Settings",
                        .enabled = [&nav] { return nav.page() == Page::Settings; },
                        .run =
                            [&nav](const Json&) {
                                nav.setPage(Page::Main);
                                return Json::object();
                            }});
    registerViewActions(registry, nav);
    registry.addAction({.id = "app.about",
                        .title = "About Emerick",
                        .aliases = {"version"},
                        .category = "General",
                        .run =
                            [&registry](const Json&) {
                                registry.reveal("about");
                                return Json{{"name", "Emerick"}, {"version", appVersion()}};
                            },
                        .menus = {kHelpMenu}});
    registry.addAction({.id = "app.quit",
                        .title = "Quit Emerick",
                        .aliases = {"exit", "close app"},
                        .category = "General",
                        .shortcut = "Ctrl+Q",
                        .run =
                            [](const Json&) {
                                QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
                                return Json{{"quitting", true}};
                            },
                        .menus = {kFileMenu}});

    registry.addSetting({.id = "appearance.accent",
                         .title = "Accent color",
                         .aliases = {"theme color", "highlight color"},
                         .category = "Settings / Appearance",
                         .type = SettingType::Color,
                         .defaultValue = "#F2B544",
                         .readOnly = true,
                         .location = "settings.general"});
    registry.addSetting({.id = "mcp.port",
                         .title = "MCP port",
                         .aliases = {"agent port", "server port"},
                         .category = "Settings / Agents",
                         .type = SettingType::Integer,
                         .defaultValue = 8888,
                         .minimum = 0,
                         .maximum = kMaxPort,
                         .hint = "Restart Emerick to apply",
                         .location = "settings.general"});
    registry.addSetting({.id = "audio.bufferSize",
                         .title = "Audio buffer size",
                         .aliases = {"latency", "block size"},
                         .category = "Settings / Audio",
                         .type = SettingType::Choice,
                         .defaultValue = 256,
                         .choices = {64, 128, 256, 512, 1024, 2048},
                         .hint = "Frames the device processes per cycle. Restart Emerick to apply",
                         .location = "settings.audio"});
    registry.addSetting({.id = "audio.sampleRate",
                         .title = "Sample rate",
                         .aliases = {"hz", "rate"},
                         .category = "Settings / Audio",
                         .type = SettingType::Choice,
                         .defaultValue = 48000,
                         .choices = {44100, 48000, 88200, 96000},
                         .hint = "Rate every device in the project runs at. Restart Emerick to apply",
                         .location = "settings.audio"});
    registry.addSetting({.id = "audio.recordingDelay",
                         .title = "Recording delay",
                         .aliases = {"latency compensation", "record offset"},
                         .category = "Settings / Audio",
                         .type = SettingType::Integer,
                         .defaultValue = 0,
                         .minimum = 0,
                         .maximum = kMaxRecordingDelay,
                         .hint = "Samples to move recorded audio earlier, beyond the latency the device reports. "
                                 "Restart Emerick to apply",
                         .location = "settings.audio"});
}

}  // namespace emerick::app
