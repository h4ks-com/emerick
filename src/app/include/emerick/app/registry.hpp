#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "emerick/app/enums.hpp"
#include "emerick/core/fuzzy.hpp"
#include "emerick/core/time.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

using Json = nlohmann::json;

struct AppError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The menu bar lists these menus in this order. A menu path such as "Plugins/Yamaha Arranger" shows as a submenu.
inline constexpr const char* kFileMenu = "File";
inline constexpr const char* kEditMenu = "Edit";
inline constexpr const char* kTrackMenu = "Track";
inline constexpr const char* kTransportMenu = "Transport";
inline constexpr const char* kViewMenu = "View";
inline constexpr const char* kSettingsMenu = "Settings";
inline constexpr const char* kScriptsMenu = "Scripts";
inline constexpr const char* kPluginsMenu = "Plugins";
inline constexpr const char* kHelpMenu = "Help";
inline constexpr std::array kMenuBar{kFileMenu,     kEditMenu,    kTrackMenu,   kTransportMenu, kViewMenu,
                                     kSettingsMenu, kScriptsMenu, kPluginsMenu, kHelpMenu};
// The menu behind the document button in the header.
inline constexpr const char* kProjectMenu = "Project";
// Context menus, which run their actions with the clicked track or the selected clips.
inline constexpr const char* kTrackContextMenu = "Track context";
inline constexpr const char* kClipContextMenu = "Clip context";
inline constexpr const char* kLaneContextMenu = "Tracks area context";

// We give every field a default so callers' designated initializers can skip it; this check calls those defaults
// redundant. NOLINTBEGIN(readability-redundant-member-init)

// A UI activation without params asks for this param with a file dialog; an agent passes it.
struct FileDialog {
    std::string param = {};
    FileDialogMode mode = FileDialogMode::Open;
    std::string nameFilter = {};
    std::string suffix = {};
};

// One entry of an action's submenu, such as one recent project.
struct MenuChoice {
    std::string id = {};
    std::string title = {};
    Json params = {};
};

struct Action {
    std::string id = {};
    std::string title = {};
    std::string description = {};
    std::vector<std::string> aliases = {};
    std::vector<std::string> keywords = {};
    std::string category = {};
    std::string shortcut = {};
    // More keys for the same action, such as Delete beside Backspace.
    std::vector<std::string> extraShortcuts = {};
    std::function<bool()> enabled = {};
    Json schema = {};
    std::function<Json(const Json& params)> run = {};
    std::string location = {};
    std::vector<std::string> menus = {};
    std::optional<FileDialog> dialog = {};
    // A UI activation asks this question first and runs the action with confirm: true.
    std::string confirm = {};
    std::function<std::vector<MenuChoice>()> submenu = {};
    // The action log skips a quiet action and logs the actions it runs instead, as for the console's own actions.
    bool quiet = false;
    // An action that runs Lua, which MCP refuses when it serves without lua.eval.
    bool runsLua = false;
};

struct Marker {
    core::Ticks at = 0;
    std::string text = {};
};

struct Setting {
    std::string id = {};
    std::string title = {};
    std::vector<std::string> aliases = {};
    std::string category = {};
    SettingType type = SettingType::String;
    Json defaultValue = {};
    Json choices = Json::array();
    std::optional<double> minimum = {};
    std::optional<double> maximum = {};
    bool readOnly = false;
    std::string hint = {};
    // Values the settings page offers for a String setting as {id, name} entries; typing any other text still works.
    std::function<Json()> suggestions = {};
    std::string location = {};
    // Reports the current value of a read-only setting the app measures, such as the audio latency.
    std::function<Json()> value = {};
};

struct RouteSections {
    bool audio = false;
    bool midi = false;
};

struct PlaybackContext {
    core::TempoMap tempo = {};
    std::filesystem::path folder = {};
};

struct TakeContext {
    core::TempoMap tempo = {};
    std::filesystem::path folder = {};
    core::Ticks stopTicks = 0;
    double sampleRate = 0.0;
};

struct TrackType {
    std::string kind = {};
    std::string title = {};
    std::string icon = {};
    std::string headerComponent = {};
    std::string laneComponent = {};
    RouteSections routes = {};
    std::function<engine::Track(const model::Track&, const PlaybackContext&)> toEngine = {};
    std::function<void(model::Track&, const engine::RecordedTake&, const TakeContext&)> fromTake = {};
    // Names along the timeline, such as chords, that a MIDI export writes as markers.
    std::function<std::vector<Marker>(const model::Track&)> markers = {};
};

struct MidiDeviceProfile {
    std::string nameMatch = {};
    std::map<int, std::string> channelNames = {};
    std::map<int, std::string> channelIcons = {};
};

struct Location {
    std::string id = {};
    std::string parent = {};
    std::string title = {};
    // The heading a settings section lists under, such as Plugins; empty lists it with the built-in sections.
    std::string group = {};
    std::function<void()> reveal = {};
};
// NOLINTEND(readability-redundant-member-init)

struct SearchHit {
    EntryKind kind = EntryKind::Action;
    std::string id;
    std::string title;
    std::string category;
    std::string shortcut;
    std::string location;
    core::FuzzyMatch match;
};

struct Revealed {
    std::string target;
    std::vector<std::string> chain;
};

class Registry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settingsMap NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList keyCommands READ keyCommands NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantMap menus READ menus NOTIFY settingsChanged)
    Q_PROPERTY(QStringList menuBar READ menuBar NOTIFY settingsChanged)

public:
    // An empty path uses the platform's default QSettings store.
    explicit Registry(const QString& settingsPath = {}, QObject* parent = nullptr);

    void addAction(Action entry);
    void removeAction(const std::string& id);
    void addSetting(Setting entry);
    void addTrackType(TrackType type);
    void addMidiDeviceProfile(MidiDeviceProfile profile);
    void addLocation(Location location);
    void addStateProvider(const std::string& name, std::function<Json()> provider);
    // Runs edits as one undo step named name; the project installs it, so runActions can group any actions.
    void setUndoGroup(std::function<void(const std::string& name, const std::function<void()>& edits)> group);

    [[nodiscard]] const Action* action(const std::string& id) const;
    [[nodiscard]] const Setting* setting(const std::string& id) const;
    [[nodiscard]] const std::vector<Action>& actions() const { return actions_; }
    [[nodiscard]] const std::vector<Setting>& allSettings() const { return settings_; }
    [[nodiscard]] const std::vector<TrackType>& trackTypes() const { return trackTypes_; }
    [[nodiscard]] const TrackType* trackType(const std::string& kind) const;
    [[nodiscard]] const MidiDeviceProfile* midiDeviceProfile(const std::string& deviceName) const;

    // Emits actionRan after an action that no other action ran, so the log shows what a person or agent asked for.
    Json run(const std::string& id, const Json& params = Json::object(),
             ActionSource source = ActionSource::Code) const;
    [[nodiscard]] Json settingValue(const std::string& id) const;
    Json setSetting(const std::string& id, const Json& value);
    // Saves the shortcut in the settings store and takes it from any action that had it; an empty sequence clears it.
    Json setShortcut(const std::string& id, const std::string& sequence);
    [[nodiscard]] Json state(const std::string& name) const;
    [[nodiscard]] Json allState() const;

    [[nodiscard]] std::vector<SearchHit> search(const std::string& query, std::size_t limit = 50) const;
    [[nodiscard]] Json describeActions(const std::string& query) const;
    [[nodiscard]] Json describeSetting(const std::string& id) const;
    [[nodiscard]] Json describeSettings() const;
    // Applies every location step from the root to the entry's location and names the item to wait for.
    Revealed reveal(const std::string& id);

    [[nodiscard]] QVariantMap settingsMap() const;
    [[nodiscard]] QVariantList shortcuts() const;
    // Each menu's items in registration order. A menu bar item binds its shortcut when the key has Ctrl or Alt and
    // is not a text editing key, so the native menu shows it; the window binds every other shortcut.
    [[nodiscard]] QVariantMap menus() const;
    // The menu bar menus that hold any action, in kMenuBar order.
    [[nodiscard]] QStringList menuBar() const;
    // Every action with its title, category and shortcut, for the key commands list.
    [[nodiscard]] QVariantList keyCommands() const;
    // The portable text of a key press, such as Ctrl+Shift+S, or empty for a modifier alone.
    Q_INVOKABLE static QString keySequence(int key, int modifiers);
    Q_INVOKABLE QVariantList palette(const QString& query) const;
    Q_INVOKABLE QVariantMap actionInfo(const QString& id) const;
    Q_INVOKABLE QVariantList sections(const QString& parent) const;
    Q_INVOKABLE QVariantList settingsAt(const QString& location) const;
    // The UI runs actions through here. An action with a file dialog or a confirm question asks the user first and
    // runs from the answer.
    Q_INVOKABLE void activate(const QString& id);
    // QML gets an empty value for a failed action; we log the failure.
    Q_INVOKABLE QVariant runAction(const QString& id, const QVariantMap& params) const;
    // Runs each {id, params} call as one undo step named name, such as a drag across toggles or an edit of every
    // selected clip. A call that fails is logged and the others still run.
    Q_INVOKABLE void runActions(const QString& name, const QVariantList& calls) const;
    // Runs the action with the file a dialog picked as its dialog param.
    Q_INVOKABLE QVariant runWithFile(const QString& id, const QUrl& file) const;
    Q_INVOKABLE bool setSettingValue(const QString& id, const QVariant& value);

signals:
    void settingsChanged();
    void shortcutsChanged();
    void fileDialogRequested(const QVariantMap& request);
    void confirmRequested(const QVariantMap& request);
    void actionRan(const std::string& id, const nlohmann::json& params, emerick::app::ActionSource source) const;

private:
    std::unique_ptr<QSettings> store_;
    std::vector<Action> actions_;
    std::vector<Setting> settings_;
    std::vector<TrackType> trackTypes_;
    std::vector<MidiDeviceProfile> midiDeviceProfiles_;
    std::vector<Location> locations_;
    std::map<std::string, std::function<Json()>> stateProviders_;
    std::function<void(const std::string&, const std::function<void()>&)> undoGroup_;
    mutable int runDepth_ = 0;
};

QString qs(const std::string& text);
QVariant toVariant(const Json& value);
Json toJson(const QVariant& value);

}  // namespace emerick::app
