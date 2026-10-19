#include "emerick/app/script_library.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QUrl>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <ranges>
#include <system_error>

#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

constexpr int kRescanDelayMs = 100;
constexpr const char* kExamplesResource = ":/scripts/examples";

struct HeaderField {
    std::string_view key;
    std::string ScriptHeader::* field;
};

constexpr std::array kHeaderFields{HeaderField{"title", &ScriptHeader::title},
                                   HeaderField{"shortcut", &ScriptHeader::shortcut},
                                   HeaderField{"description", &ScriptHeader::description}};

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}

std::vector<Script> scan(const std::filesystem::path& folder, ScriptScope scope) {
    std::error_code error;
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) {
        return {};
    }
    std::vector<Script> scripts;
    for (const auto& file : std::filesystem::directory_iterator(folder, error)) {
        if (file.is_regular_file(error) && file.path().extension() == ".lua") {
            const std::string stem = file.path().stem().string();
            ScriptHeader header = parseScriptHeader(readFile(file.path()));
            if (header.title.empty()) {
                header.title = stem;
            }
            scripts.push_back({"scripts." + enumKey(scope) + "." + stem, file.path(), scope, std::move(header)});
        }
    }
    std::ranges::sort(scripts, {}, &Script::id);
    return scripts;
}

std::string portableShortcut(const std::string& sequence) {
    return QKeySequence::fromString(qs(sequence), QKeySequence::PortableText)
        .toString(QKeySequence::PortableText)
        .toStdString();
}

Json describe(const Script& script) {
    return {{"id", script.id},
            {"title", script.header.title},
            {"description", script.header.description},
            {"shortcut", script.header.shortcut},
            {"scope", enumKey(script.scope)},
            {"path", script.path.string()}};
}

}  // namespace

ScriptHeader parseScriptHeader(std::string_view source) {
    ScriptHeader header;
    for (const auto part : source | std::views::split('\n')) {
        const std::string_view line = trimmed(std::string_view{part});
        if (line.empty()) {
            continue;
        }
        if (!line.starts_with("--")) {
            break;
        }
        const std::string_view comment = line.substr(2);
        const auto colon = comment.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trimmed(comment.substr(0, colon));
        const auto* const found = std::ranges::find(kHeaderFields, key, &HeaderField::key);
        if (found != kHeaderFields.end()) {
            header.*found->field = std::string{trimmed(comment.substr(colon + 1))};
        }
    }
    return header;
}

ScriptLibrary::ScriptLibrary(Registry& registry, Console& console, std::filesystem::path userFolder, QObject* parent)
    : QObject(parent), registry_(registry), console_(console), userFolder_(std::move(userFolder)) {
    rescanTimer_.setSingleShot(true);
    rescanTimer_.setInterval(kRescanDelayMs);
    connect(&rescanTimer_, &QTimer::timeout, this, &ScriptLibrary::rescan);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &rescanTimer_, qOverload<>(&QTimer::start));
    connect(&watcher_, &QFileSystemWatcher::fileChanged, &rescanTimer_, qOverload<>(&QTimer::start));
    rescan();
}

void ScriptLibrary::setProjectFolder(const std::filesystem::path& folder) {
    if (folder != projectFolder_) {
        projectFolder_ = folder;
        rescan();
    }
}

void ScriptLibrary::rescan() {
    std::vector<Script> found = scan(userFolder_, ScriptScope::User);
    std::ranges::move(scan(projectFolder_, ScriptScope::Project), std::back_inserter(found));
    if (found != scripts_) {
        for (const Script& script : scripts_) {
            registry_.removeAction(script.id);
        }
        scripts_ = std::move(found);
        for (const Script& script : scripts_) {
            registerScript(script);
        }
    }
    watch();
}

const Script* ScriptLibrary::find(const std::string& nameOrPath) const {
    const auto found = std::ranges::find_if(scripts_, [&](const Script& script) {
        return script.id == nameOrPath || script.header.title == nameOrPath || script.path.stem() == nameOrPath ||
               script.path.filename() == nameOrPath || script.path == std::filesystem::path{nameOrPath};
    });
    return found == scripts_.end() ? nullptr : &*found;
}

Json ScriptLibrary::run(const Script& script) const { return console_.runScript(script.path); }

void ScriptLibrary::watch() {
    if (!watcher_.files().isEmpty()) {
        watcher_.removePaths(watcher_.files());
    }
    if (!watcher_.directories().isEmpty()) {
        watcher_.removePaths(watcher_.directories());
    }
    QStringList paths;
    for (const auto& folder : {userFolder_, projectFolder_}) {
        std::error_code error;
        if (!folder.empty() && std::filesystem::is_directory(folder, error)) {
            paths.append(QString::fromStdString(folder.string()));
        }
    }
    for (const Script& script : scripts_) {
        paths.append(QString::fromStdString(script.path.string()));
    }
    if (!paths.isEmpty()) {
        watcher_.addPaths(paths);
    }
}

void ScriptLibrary::registerScript(const Script& script) {
    std::string shortcut = portableShortcut(script.header.shortcut);
    // A script never takes a key another action already has; Key Commands can still give it one.
    if (std::ranges::any_of(registry_.actions(),
                            [&](const Action& entry) { return portableShortcut(entry.shortcut) == shortcut; })) {
        shortcut.clear();
    }
    registry_.addAction({.id = script.id,
                         .title = script.header.title,
                         .description = script.header.description,
                         .aliases = {script.path.stem().string()},
                         .keywords = {"script", enumKey(script.scope) + " script"},
                         .category = "Scripts",
                         .shortcut = shortcut,
                         .run = [this, path = script.path](const Json&) { return console_.runScript(path); },
                         .menus = {kScriptsMenu},
                         .runsLua = true});
}

void copyExampleScripts(const std::filesystem::path& folder) {
    std::error_code error;
    if (std::filesystem::exists(folder, error) && !std::filesystem::is_empty(folder, error)) {
        return;
    }
    std::filesystem::create_directories(folder, error);
    if (error) {
        return;
    }
    const QDir examples(kExamplesResource);
    for (const QString& name : examples.entryList(QDir::Files)) {
        const QString target = QDir(QString::fromStdString(folder.string())).filePath(name);
        // Resource files copy read only, so we make each copy editable.
        if (QFile::copy(examples.filePath(name), target)) {
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        }
    }
}

void registerScriptActions(Registry& registry, ScriptLibrary& library) {
    registry.addAction(
        {.id = "script.run",
         .title = "Run script",
         .aliases = {"run lua file"},
         .category = "Scripts",
         .schema = {{"properties", {{"name", {{"type", "string"}}}, {"path", {{"type", "string"}}}}}},
         .run =
             [&library](const Json& params) {
                 const auto name = optionalParam<std::string>(params, "name");
                 const auto path = optionalParam<std::string>(params, "path");
                 if (!name && !path) {
                     throw AppError("pass name or path; script.list lists the scripts");
                 }
                 if (const Script* script = library.find(name ? *name : *path)) {
                     return library.run(*script);
                 }
                 std::error_code error;
                 if (path && std::filesystem::is_regular_file(*path, error)) {
                     return library.run({.id = *path, .path = *path, .scope = ScriptScope::User, .header = {}});
                 }
                 throw AppError("no script " + (name ? *name : *path) + "; script.list lists the scripts");
             },
         .runsLua = true});
    registry.addAction(
        {.id = "script.list", .title = "List scripts", .category = "Scripts", .run = [&library](const Json&) {
             return Json{
                 {"scripts", library.scripts() | std::views::transform(describe) | std::ranges::to<Json::array_t>()},
                 {"userFolder", library.userFolder().string()},
                 {"projectFolder", library.projectFolder().string()}};
         }});
    registry.addAction({.id = "script.openFolder",
                        .title = "Open scripts folder",
                        .aliases = {"show scripts", "reveal scripts folder"},
                        .category = "Scripts",
                        .schema = {{"properties", {{"scope", {{"type", "string"}, {"enum", {"user", "project"}}}}}}},
                        .run =
                            [&library](const Json& params) {
                                const auto key = optionalParam<std::string>(params, "scope").value_or("user");
                                const auto scope = enumFromKey<ScriptScope>(key);
                                if (!scope) {
                                    throw AppError("scope is user or project, got " + key);
                                }
                                const std::filesystem::path folder =
                                    *scope == ScriptScope::User ? library.userFolder() : library.projectFolder();
                                std::error_code error;
                                std::filesystem::create_directories(folder, error);
                                if (error) {
                                    throw AppError("cannot create " + folder.string() + ": " + error.message());
                                }
                                library.rescan();
                                QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(folder.string())));
                                return Json{{"path", folder.string()}};
                            },
                        .menus = {kScriptsMenu}});
}

}  // namespace emerick::app
