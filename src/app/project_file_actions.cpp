#include <QtSystemDetection>
#ifdef Q_OS_MACOS
#include <QProcess>
#else
#include <QDesktopServices>
#include <QUrl>
#endif
#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"

namespace emerick::app {

namespace {

constexpr const char* kRecentSetting = "project.recent";
constexpr std::size_t kMaxRecent = 10;

std::vector<std::string> recentProjects(const Registry& registry) {
    return registry.settingValue(kRecentSetting).get<std::vector<std::string>>() |
           std::views::filter([](const std::string& path) { return std::filesystem::exists(path); }) |
           std::ranges::to<std::vector>();
}

void rememberRecent(Registry& registry, const std::filesystem::path& folder) {
    std::vector<std::string> recent = recentProjects(registry);
    std::erase(recent, folder.string());
    recent.insert(recent.begin(), folder.string());
    recent.resize(std::min(recent.size(), kMaxRecent));
    if (recent != registry.settingValue(kRecentSetting).get<std::vector<std::string>>()) {
        registry.setSetting(kRecentSetting, recent);
    }
}

std::vector<MenuChoice> recentChoices(const Registry& registry) {
    return recentProjects(registry) | std::views::transform([](const std::string& path) {
               return MenuChoice{
                   .id = path, .title = std::filesystem::path{path}.stem().string(), .params = {{"path", path}}};
           }) |
           std::ranges::to<std::vector>();
}

void reveal(const std::filesystem::path& folder) {
#ifdef Q_OS_MACOS
    QProcess::startDetached("open", {"-R", qs(folder.string())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(qs(folder.parent_path().string())));
#endif
}

#ifdef Q_OS_MACOS
constexpr const char* kRevealTitle = "Show in Finder";
#else
constexpr const char* kRevealTitle = "Show in file manager";
#endif

}  // namespace

void registerProjectFileActions(Registry& registry, ProjectController& controller) {
    registry.addSetting({.id = kRecentSetting,
                         .title = "Recent projects",
                         .category = "Project",
                         .type = SettingType::List,
                         .defaultValue = Json::array()});
    rememberRecent(registry, controller.folder());
    QObject::connect(&controller, &ProjectController::fileChanged, &registry,
                     [&registry, &controller] { rememberRecent(registry, controller.folder()); });
    const auto startNew = [&controller](const Json&) {
        controller.newProject();
        return projectState(controller);
    };
    const auto openPath = [&controller](const Json& params) {
        controller.open(param<std::string>(params, "path"));
        return projectState(controller);
    };

    registry.addAction({.id = "project.new",
                        .title = "New project",
                        .aliases = {"start over", "empty project"},
                        .category = "Project",
                        .shortcut = "Ctrl+N",
                        .run = startNew,
                        .menus = {kFileMenu, kProjectMenu}});
    registry.addAction({.id = "project.open",
                        .title = "Open project",
                        .aliases = {"load project"},
                        .category = "Project",
                        .shortcut = "Ctrl+O",
                        .schema = {{"required", {"path"}}},
                        .run = openPath,
                        .menus = {kFileMenu, kProjectMenu},
                        .dialog = FileDialog{.param = "path",
                                             .mode = FileDialogMode::Open,
                                             .nameFilter = "Emerick projects (*.erk project.json)"}});
    registry.addAction({.id = "project.openRecent",
                        .title = "Open recent project",
                        .aliases = {"recent projects"},
                        .category = "Project",
                        .schema = {{"required", {"path"}}},
                        .run = openPath,
                        .menus = {kFileMenu, kProjectMenu},
                        .submenu = [&registry] { return recentChoices(registry); }});
    registry.addAction({.id = "project.close",
                        .title = "Close project",
                        .category = "Project",
                        .shortcut = "Ctrl+W",
                        .run = startNew,
                        .menus = {kFileMenu}});
    registry.addAction({.id = "project.save",
                        .title = "Save project",
                        .category = "Project",
                        .shortcut = "Ctrl+S",
                        .run =
                            [&controller](const Json&) {
                                controller.save();
                                return projectState(controller);
                            },
                        .menus = {kFileMenu}});
    registry.addAction({.id = "project.saveAs",
                        .title = "Save project as",
                        .aliases = {"save to folder", "rename project"},
                        .category = "Project",
                        .shortcut = "Ctrl+Shift+S",
                        .schema = {{"required", {"path"}}},
                        .run =
                            [&controller](const Json& params) {
                                controller.saveAs(param<std::string>(params, "path"));
                                return projectState(controller);
                            },
                        .menus = {kFileMenu, kProjectMenu},
                        .dialog = FileDialog{.param = "path",
                                             .mode = FileDialogMode::Save,
                                             .nameFilter = "Emerick projects (*.erk)",
                                             .suffix = "erk"}});
}

void registerProjectFolderActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "project.reveal",
                        .title = kRevealTitle,
                        .aliases = {"reveal project", "open project folder"},
                        .category = "Project",
                        .run =
                            [&controller](const Json&) {
                                reveal(controller.folder());
                                return Json{{"revealed", controller.folder().string()}};
                            },
                        .menus = {kFileMenu, kProjectMenu}});
    registry.addAction({.id = "project.removeUnusedAudio",
                        .title = "Remove unused audio files",
                        .aliases = {"clean up project", "delete unused takes"},
                        .category = "Project",
                        .run =
                            [&controller](const Json& params) {
                                if (!optionalParam<bool>(params, "confirm").value_or(false)) {
                                    throw AppError("pass confirm: true to delete the audio files no clip uses");
                                }
                                return Json{{"removed", controller.removeUnusedAudio()}};
                            },
                        .menus = {kFileMenu},
                        .confirm = "Delete the audio files that no clip uses? Undo cannot bring them back."});
    registry.addAction({.id = "project.showMenu",
                        .title = "Show or hide project menu",
                        .aliases = {"file menu", "document menu"},
                        .category = "Project",
                        .run = [&nav](const Json&) {
                            nav.setProjectMenuOpen(!nav.projectMenuOpen());
                            return Json{{"projectMenuOpen", nav.projectMenuOpen()}};
                        }});
}

}  // namespace emerick::app
