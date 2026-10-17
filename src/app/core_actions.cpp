#include "emerick/app/core_actions.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <array>
#include <memory>
#include <string>

namespace emerick::app {

namespace {

constexpr int kMaxPort = 65535;

Json revealChain(Registry& registry, const std::string& id) { return Json{{"chain", registry.reveal(id).chain}}; }

struct ViewStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    double zoom;
    int height;
};

constexpr double kZoomStep = 1.4142135623730951;
constexpr int kTrackHeightStep = 12;

constexpr std::array kViewSteps{
    ViewStep{"view.zoomIn", "Zoom in", "wider timeline", "Ctrl+=", kZoomStep, 0},
    ViewStep{"view.zoomOut", "Zoom out", "narrower timeline", "Ctrl+-", 1 / kZoomStep, 0},
    ViewStep{"view.trackTaller", "Make tracks taller", "vertical zoom in", "Ctrl+Down", 1, kTrackHeightStep},
    ViewStep{"view.trackShorter", "Make tracks shorter", "vertical zoom out", "Ctrl+Up", 1, -kTrackHeightStep},
};

void registerViewActions(Registry& registry, Nav& nav) {
    for (const ViewStep& step : kViewSteps) {
        registry.addAction({.id = step.id,
                            .title = step.title,
                            .aliases = {step.alias},
                            .category = "View",
                            .shortcut = step.shortcut,
                            .run = [&nav, step](const Json&) {
                                nav.setPixelsPerTick(nav.pixelsPerTick() * step.zoom);
                                nav.setTrackHeight(nav.trackHeight() + step.height);
                                return Json{{"pixelsPerTick", nav.pixelsPerTick()}, {"trackHeight", nav.trackHeight()}};
                            }});
    }
}

}  // namespace

const char* appVersion() { return EMERICK_VERSION; }

void addSettingsSection(Registry& registry, Nav& nav, const std::string& key, const std::string& title) {
    // Copies of the handlers share the key, so copying a handler never allocates.
    auto section = std::make_shared<const std::string>(key);
    registry.addLocation({.id = "settings." + key, .parent = "settings", .title = title, .reveal = [&nav, section] {
                              nav.setSettingsTab(qs(*section));
                          }});
    registry.addAction(
        {.id = "settings.show." + key,
         .title = "Show " + key + " settings",
         .aliases = {key + " preferences"},
         .category = "Settings",
         .run = [&registry, section](const Json&) { return revealChain(registry, "settings." + *section); }});
}

void registerCoreActions(Registry& registry, Nav& nav, const Backends& backends) {
    registry.addStateProvider("nav", [&nav] { return nav.toJson(); });
    // Copies of the provider share these names, so copying the provider never allocates.
    auto names = std::make_shared<const Backends>(backends);
    registry.addStateProvider(
        "app", [names] { return Json{{"version", appVersion()}, {"audio", names->audio}, {"midi", names->midi}}; });

    registry.addLocation({.id = "palette", .reveal = [&nav] { nav.setPaletteOpen(true); }});
    registry.addLocation({.id = "about", .reveal = [&nav] { nav.setAboutOpen(true); }});
    registry.addLocation({.id = "settings", .reveal = [&nav] {
                              nav.setPaletteOpen(false);
                              nav.setPage(Page::Settings);
                          }});
    addSettingsSection(registry, nav, "general", "General");
    addSettingsSection(registry, nav, "audio", "Audio");

    registry.addAction({.id = "palette.open",
                        .title = "Open command palette",
                        .aliases = {"command palette", "search actions"},
                        .category = "General",
                        .shortcut = "Ctrl+K",
                        .run = [&registry](const Json&) { return revealChain(registry, "palette"); }});
    registry.addAction({.id = "settings.open",
                        .title = "Open settings",
                        .aliases = {"preferences", "options"},
                        .category = "General",
                        .shortcut = "Ctrl+,",
                        .run = [&registry](const Json&) { return revealChain(registry, "settings"); }});
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
                        .run = [&registry](const Json&) {
                            registry.reveal("about");
                            return Json{{"name", "Emerick"}, {"version", appVersion()}};
                        }});
    registry.addAction({.id = "app.quit",
                        .title = "Quit Emerick",
                        .aliases = {"exit", "close app"},
                        .category = "General",
                        .shortcut = "Ctrl+Q",
                        .run = [](const Json&) {
                            QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
                            return Json{{"quitting", true}};
                        }});

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
                         .defaultValue = 7777,
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
                         .hint = "Frames per block",
                         .location = "settings.audio"});
}

}  // namespace emerick::app
