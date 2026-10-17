#include "emerick/app/core_actions.hpp"

#include <QCoreApplication>
#include <QMetaEnum>
#include <QTimer>
#include <memory>

namespace emerick::app {

namespace {

constexpr int kMaxPort = 65535;

Json revealChain(Registry& registry, const std::string& id) { return Json{{"chain", registry.reveal(id).chain}}; }

void registerSettingsSections(Registry& registry, Nav& nav) {
    const QMetaEnum tabs = QMetaEnum::fromType<SettingsTab>();
    for (int index = 0; index < tabs.keyCount(); ++index) {
        const auto tab = static_cast<SettingsTab>(tabs.value(index));
        const std::string section = enumKey(tab);
        const std::string location = "settings." + section;
        registry.addLocation({.id = location, .parent = "settings", .title = enumTitle(tab), .reveal = [&nav, tab] {
                                  nav.setSettingsTab(tab);
                              }});
        registry.addAction(
            {.id = "settings.show." + section,
             .title = "Show " + section + " settings",
             .aliases = {section + " preferences"},
             .category = "Settings",
             .run = [&registry, tab](const Json&) { return revealChain(registry, "settings." + enumKey(tab)); }});
    }
}

}  // namespace

const char* appVersion() { return EMERICK_VERSION; }

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
    registerSettingsSections(registry, nav);

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
