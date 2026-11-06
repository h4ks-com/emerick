#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/console.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

// The comment lines at the top of a script, such as "-- title: Mute all" and "-- shortcut: Ctrl+Alt+M".
struct ScriptHeader {
    std::string title;
    std::string shortcut;
    std::string description;
    auto operator<=>(const ScriptHeader&) const = default;
};

ScriptHeader parseScriptHeader(std::string_view source);

struct Script {
    std::string id;
    std::filesystem::path path;
    ScriptScope scope = ScriptScope::User;
    ScriptHeader header;
    auto operator<=>(const Script&) const = default;
};

// The .lua files in the user and project script folders, each registered as an action in the Scripts menu. We scan
// the folders again whenever a file in them changes.
class ScriptLibrary : public QObject {
    Q_OBJECT

public:
    ScriptLibrary(Registry& registry, Console& console, std::filesystem::path userFolder, QObject* parent = nullptr);

    // The scripts folder inside the project bundle.
    void setProjectFolder(const std::filesystem::path& folder);
    void rescan();

    [[nodiscard]] const std::filesystem::path& userFolder() const { return userFolder_; }
    [[nodiscard]] const std::filesystem::path& projectFolder() const { return projectFolder_; }
    [[nodiscard]] const std::vector<Script>& scripts() const { return scripts_; }
    // A script by action id, file name, title or path.
    [[nodiscard]] const Script* find(const std::string& nameOrPath) const;
    [[nodiscard]] Json run(const Script& script) const;

private:
    void watch();
    void registerScript(const Script& script);

    Registry& registry_;
    Console& console_;
    std::filesystem::path userFolder_;
    std::filesystem::path projectFolder_;
    std::vector<Script> scripts_;
    QFileSystemWatcher watcher_;
    QTimer rescanTimer_;
};

// Copies the example scripts that ship with Emerick into folder when it is missing or empty.
void copyExampleScripts(const std::filesystem::path& folder);

// script.run, script.runFile, script.list and script.openFolder, and the scripts picker script.run opens from the UI.
void registerScriptActions(Registry& registry, ScriptLibrary& library, Nav& nav);

}  // namespace emerick::app
