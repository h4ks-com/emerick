#include <QtSystemDetection>
#ifdef Q_OS_MACOS
#include <QProcess>
#else
#include <QDesktopServices>
#include <QUrl>
#endif
#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_files.hpp"
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

Json collectedState(ProjectController& controller, const CollectedFiles& collected) {
    Json state = projectState(controller);
    state["copied"] = collected.copied;
    state["missing"] = collected.missing;
    return state;
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
    registry.addSetting({.id = std::string{kAutosaveSetting},
                         .title = "Autosave",
                         .aliases = {"save automatically"},
                         .category = "Project",
                         .type = SettingType::Boolean,
                         .defaultValue = true,
                         .hint = "Save the project a moment after each edit and when you leave it. Turn it off to "
                                 "save only when you choose Save",
                         .location = "settings.general"});
    rememberRecent(registry, controller.folder());
    QObject::connect(&controller, &ProjectController::fileChanged, &registry,
                     [&registry, &controller] { rememberRecent(registry, controller.folder()); });
    const auto startNew = [&controller](const Json&) {
        controller.newProject();
        return projectState(controller);
    };
    const auto createNew = [&registry, &controller](const Json& params) {
        const core::TempoMap tempo = tempoFromParams(params, core::TempoMap{});
        const auto path = optionalParam<std::string>(params, "path");
        const std::optional<std::filesystem::path> target =
            path ? std::optional{registry.checkedPath(*path, PathAccess::Write)} : std::nullopt;
        if (target && (std::filesystem::exists(*target) || std::filesystem::exists(target->string() + ".erk"))) {
            throw AppError(target->string() + " already exists, pass another path");
        }
        controller.newProject();
        if (params.contains("bpm") || params.contains("numerator") || params.contains("denominator")) {
            controller.project().setTempo(tempo);
        }
        if (target) {
            controller.saveAs(*target);
        }
        return projectState(controller);
    };
    const auto openPath = [&registry, &controller](const Json& params) {
        controller.open(registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read));
        return projectState(controller);
    };

    registry.addAction({.id = "project.new",
                        .title = "New project",
                        .description = "Starts an untitled project, or one saved at path, with an optional tempo.",
                        .aliases = {"start over", "empty project"},
                        .category = "Project",
                        .shortcut = "Ctrl+N",
                        .schema = {{"properties",
                                    {{"path", {{"type", "string"}}},
                                     {"bpm", {{"type", "number"}}},
                                     {"numerator", {{"type", "integer"}}},
                                     {"denominator", {{"type", "integer"}}}}}},
                        .run = createNew,
                        .menus = {kFileMenu, kProjectMenu}});
    registry.addAction(
        {.id = "project.open",
         .title = "Open project",
         .aliases = {"load project"},
         .category = "Project",
         .shortcut = "Ctrl+O",
         .schema = {{"required", {"path"}}},
         .run = openPath,
         .menus = {kFileMenu, kProjectMenu},
         .dialog = FileDialog{
             .param = "path", .mode = FileDialogMode::Open, .nameFilter = "Emerick projects (*.erk project.json)"}});
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
                        .description = "Saves the project, and with collect first copies the files it names from "
                                       "outside its bundle into it.",
                        .category = "Project",
                        .shortcut = "Ctrl+S",
                        .schema = {{"properties", {{"collect", {{"type", "boolean"}}}}}},
                        .run =
                            [&registry, &controller](const Json& params) {
                                const std::optional<CollectedFiles> collected =
                                    optionalParam<bool>(params, "collect").value_or(false)
                                        ? std::optional{collectFiles(registry, controller)}
                                        : std::nullopt;
                                controller.save();
                                return collected ? collectedState(controller, *collected) : projectState(controller);
                            },
                        .menus = {kFileMenu}});
    registry.addAction(
        {.id = "project.collectFiles",
         .title = "Collect project files",
         .description = "Copies every file the project names from outside its bundle into the bundle, "
                        "audio into audio/ and styles, SoundFonts and voice folders into files/, and "
                        "points the project at the copies as one undo step.",
         .aliases = {"consolidate project", "copy files into project"},
         .category = "Project",
         .run = [&registry,
                 &controller](const Json&) { return collectedState(controller, collectFiles(registry, controller)); },
         .menus = {kFileMenu}});
    registry.addAction({.id = "project.saveAs",
                        .title = "Save project as",
                        .aliases = {"save to folder", "rename project"},
                        .category = "Project",
                        .shortcut = "Ctrl+Shift+S",
                        .schema = {{"required", {"path"}}},
                        .run =
                            [&registry, &controller](const Json& params) {
                                controller.saveAs(
                                    registry.checkedPath(param<std::string>(params, "path"), PathAccess::Write));
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
                        .menus = {kFileMenu, kProjectMenu},
                        .windowOnly = true});
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
    registry.addLocation({.id = "health", .reveal = [&nav] { nav.setHealthOpen(true); }});
    registry.addAction({.id = "project.health",
                        .title = "Check project health",
                        .aliases = {"missing files", "find problems", "project issues"},
                        .category = "Project",
                        .run =
                            [&registry, &controller](const Json&) {
                                const Json issues = controller.checkHealth();
                                registry.reveal("health");
                                return Json{{"issues", issues}};
                            },
                        .menus = {kFileMenu}});
    registry.addAction({.id = "project.showMenu",
                        .title = "Show or hide project menu",
                        .aliases = {"file menu", "document menu"},
                        .category = "Project",
                        .run =
                            [&nav](const Json&) {
                                nav.setProjectMenuOpen(!nav.projectMenuOpen());
                                return Json{{"projectMenuOpen", nav.projectMenuOpen()}};
                            },
                        .windowOnly = true});
}

}  // namespace emerick::app
