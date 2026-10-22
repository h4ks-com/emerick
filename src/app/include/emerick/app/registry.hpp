#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>
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
#include "emerick/app/path_access.hpp"
#include "emerick/core/fuzzy.hpp"
#include "emerick/core/time.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

using Json = nlohmann::json;

struct AppError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t kMaxBatchCalls = 1000;

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
// The menus of a marker and of the marker lane, which run their actions with the clicked marker or position.
inline constexpr const char* kMarkerContextMenu = "Marker context";
inline constexpr const char* kMarkerLaneContextMenu = "Marker lane context";
// The menu the metronome button opens on a right click.
inline constexpr const char* kMetronomeMenu = "Metronome";

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
    // Computes the value while the store holds none, such as the first audio device that fits.
    std::function<Json()> fallback = {};
    // Merges a new value that may set only some fields into the current one and checks it, or throws AppError.
    std::function<Json(const Json& current, const Json& change)> merge = {};
    // An action the settings page offers as a button beside the value, such as applying it to every track.
    std::string action = {};
};

// How actions become undo steps; the project installs these, so runActions and gestures work with any actions.
struct UndoSteps {
    using Edits = std::function<void(const std::string& name, const std::function<void()>& edits)>;
    // Runs edits as one undo step named name.
    Edits group = {};
    // Replaces the open gesture's edits with these, so the document follows a drag and it stays one undo step.
    Edits updateGesture = {};
    // Returns whether the gesture left an undo step, which it does unless the document is back where it started.
    std::function<bool()> endGesture = {};
    std::function<void()> cancelGesture = {};
    // Runs edits and, when they throw, leaves the document and its undo history as they were.
    std::function<void(const std::function<void()>& edits)> atomic = {};
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
    Q_PROPERTY(bool gesturing READ gesturing NOTIFY gesturingChanged)

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
    void setUndoSteps(UndoSteps steps);

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
    // Runs each {id, params} call in order as its own run, and stops at the first failure unless keepGoing. Each
    // entry of the result holds the call's result or its error.
    Json runBatch(const Json& calls, bool keepGoing, ActionSource source) const;
    // Runs call as a request from origin; inside an outer request, the outer origin stays.
    Json runFrom(ActionSource origin, const std::function<Json()>& call) const;
    // Canonicalizes both roots, so a missing folder throws std::filesystem::filesystem_error.
    void setAgentRoots(AgentRoots roots);
    // A request from MCP or Lua may only name a path inside the agent root for that access; others name any path.
    [[nodiscard]] std::filesystem::path checkedPath(const std::string& path, PathAccess access) const;
    [[nodiscard]] Json settingValue(const std::string& id) const;
    Json setSetting(const std::string& id, const Json& given);
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
    [[nodiscard]] bool gesturing() const { return gesturing_; }
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
    // A gesture, such as a drag, runs its latest calls at most once a frame, each run replacing the last, and ends as
    // one undo step. The action log shows the last run's calls when the gesture ends. A cancelled gesture ignores
    // updates until it ends, so Escape holds through the rest of the drag.
    Q_INVOKABLE void updateGesture(const QString& name, const QVariantList& calls);
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void cancelGesture();
    // Runs the action with the file a dialog picked as its dialog param.
    Q_INVOKABLE QVariant runWithFile(const QString& id, const QUrl& file) const;
    Q_INVOKABLE bool setSettingValue(const QString& id, const QVariant& value);

signals:
    void settingsChanged();
    void shortcutsChanged();
    void fileDialogRequested(const QVariantMap& request);
    void confirmRequested(const QVariantMap& request);
    void gesturingChanged();
    void actionRan(const std::string& id, const nlohmann::json& params, emerick::app::ActionSource source) const;

private:
    std::unique_ptr<QSettings> store_;
    std::vector<Action> actions_;
    std::vector<Setting> settings_;
    std::vector<TrackType> trackTypes_;
    std::vector<MidiDeviceProfile> midiDeviceProfiles_;
    std::vector<Location> locations_;
    std::map<std::string, std::function<Json()>> stateProviders_;
    // Runs the action and logs its failure, as every UI call does.
    std::optional<Json> runFromUi(const std::string& id, const Json& params) const;
    void runGesture();
    void setGesturing(bool value);

    UndoSteps undoSteps_;
    mutable int runDepth_ = 0;
    mutable std::optional<ActionSource> origin_;
    AgentRoots agentRoots_;
    struct Call {
        std::string id;
        Json params;
    };
    std::string gestureName_;
    std::vector<Call> gestureCalls_;
    bool gesturePending_ = false;
    bool gestureCancelled_ = false;
    bool gesturing_ = false;
    QTimer gestureFrame_;
};

QString qs(const std::string& text);
QVariant toVariant(const Json& value);
Json toJson(const QVariant& value);

}  // namespace emerick::app
