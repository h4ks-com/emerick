#pragma once

#include <QDebug>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "emerick/app/enums.hpp"
#include "emerick/app/path_access.hpp"
#include "emerick/app/track_routes.hpp"
#include "emerick/app/variant.hpp"
#include "emerick/core/fuzzy.hpp"
#include "emerick/core/time.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

// Signals carry json, so Qt asks whether QDebug can print it. On Windows that probes json's implicit conversion to
// MSG, an incomplete type there, which fails to compile, so we answer the question before Qt asks it.
template <>
struct QTypeTraits::has_ostream_operator<QDebug, nlohmann::json> : std::false_type {};

namespace emerick::app {

struct AppError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t kMaxBatchCalls = 1000;

// What a run caps for the agents that drive it; a missing value leaves that limit off.
struct Limits {
    std::optional<std::chrono::milliseconds> luaTimeout;
    std::size_t luaMemoryBytes = std::size_t{256} << 20;
    std::optional<double> renderSeconds;
    bool bundledPluginsOnly = false;
    bool libraryInstall = true;
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
    // A choice that picks an icon or a track color shows it beside its title.
    std::string icon = {};
    int color = -1;
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
    // The id of the submenu choice the target of these params has now, which its menu marks.
    std::function<std::string(const Json& params)> current = {};
    // The action log skips a quiet action and logs the actions it runs instead, as for the console's own actions.
    bool quiet = false;
    // An action that runs Lua, which MCP refuses when it serves without lua.eval.
    bool runsLua = false;
    // An action that only changes the window or the desktop, which a headless run refuses.
    bool windowOnly = false;
    // A UI run tells the user the outcome: the result's message, or the error text when it fails.
    bool notifies = false;
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

struct FileDrop {
    std::vector<std::string> extensions = {};
    std::string action = {};
};

struct PlaybackContext {
    core::TempoMap tempo = {};
    std::filesystem::path folder = {};
    // The project the track plays in, for a type that plays what other tracks hold.
    const model::Document* document = nullptr;
};

struct TakeContext {
    core::TempoMap tempo = {};
    std::filesystem::path folder = {};
    core::Ticks stopTicks = 0;
    double sampleRate = 0.0;
};

// A message a track type sends; an empty port means the track's MIDI output. We drop a message for a port that no
// track, clock or metronome uses.
struct MidiMessage {
    std::string port = {};
    std::vector<std::uint8_t> bytes = {};
};

// Where a track type starts playback in place of the playhead, with the reason the transport shows, such as "Style
// starts at bar 33, Main B".
struct StartAlignment {
    core::Ticks position = 0;
    std::string reason = {};
};

struct TrackType {
    std::string kind = {};
    std::string title = {};
    std::string icon = {};
    std::string headerComponent = {};
    std::string laneComponent = {};
    // Shown in the inspector under the track's instrument, with the track's id.
    std::string inspectorComponent = {};
    RouteSections routes = {};
    // A file with one of these extensions dropped on the track runs the action with the track and the file's path.
    FileDrop fileDrop = {};
    std::function<engine::Track(const model::Track&, const PlaybackContext&)> toEngine = {};
    // True when toEngine reads other tracks of the document, so the track converts again on every change.
    bool readsOtherTracks = false;
    std::function<void(model::Track&, const engine::RecordedTake&, const TakeContext&)> fromTake = {};
    // True when fromTake reads only a take's sysex, so the preview of a take in progress keeps only its sysex.
    bool takesSysexOnly = false;
    // Names along the timeline, such as chords, that a MIDI export writes as markers.
    std::function<std::vector<Marker>(const model::Track&)> markers = {};
    // Warnings for this track or the tracks it feeds, each keyed by the track id whose header shows it.
    std::function<std::vector<engine::SessionError>(const model::Track&, const model::Document&)> warnings = {};
    // Messages the MIDI panic sends first, for every track of the type, playing or not.
    std::function<std::vector<MidiMessage>(const model::Track&)> panic = {};
    // True when a project holds MIDI Start back from the clock ports; play then sends clock only.
    std::function<bool(const model::Document&)> holdsStart = {};
    // The hooks below run only for a track that plays: unmuted, not silenced by another track's solo, with a MIDI
    // output or an instrument, which both get the messages. A project without them plays as plain MIDI.
    // Messages that put the track's device in its state at position. We send them before Start on play and record,
    // and before a locate while playing, ahead of the core chase of programs and controllers.
    std::function<std::vector<MidiMessage>(const model::Track&, core::Ticks position, const PlaybackContext&)> chase =
        {};
    // Where play from position starts, or nullopt when the track has nothing there to align. Play starts at the
    // earliest answer of all tracks; record never moves.
    std::function<std::optional<StartAlignment>(const model::Track&, core::Ticks position, const PlaybackContext&)>
        alignStart = {};
};

[[nodiscard]] inline bool playsAudio(const TrackType& type, const model::Track& track) {
    return playsAudio(type.routes, track);
}

// Why a track plays no audio, naming the fix: an instrument on a MIDI track, otherwise otherFix.
[[nodiscard]] inline std::string playsNoAudio(const TrackType* type, const model::Track& track,
                                              const std::string& otherFix) {
    return "track " + track.name + " plays no audio, " +
           (type != nullptr && type->routes.midi ? "load an instrument on this MIDI track first" : otherFix);
}

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

// Warnings for any track, such as one whose instrument reports a problem, keyed as TrackType::warnings keys them.
using DocumentWarnings = std::function<std::vector<engine::SessionError>(const model::Document&)>;
// The tracks whose instruments still load their sounds, such as Expansion Voices reading its voice packs.
using LoadingTracks = std::function<std::vector<model::TrackId>(const model::Document&)>;

class Registry : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settingsMap NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList keyCommands READ keyCommands NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantMap menus READ menus NOTIFY menusChanged)
    Q_PROPERTY(QStringList menuBar READ menuBar NOTIFY menusChanged)
    Q_PROPERTY(bool gesturing READ gesturing NOTIFY gesturingChanged)
    // The views hide every edit while this is on, as the browser viewer's registry has it.
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)

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
    void addWarnings(DocumentWarnings warnings);
    void addLoadingTracks(LoadingTracks loading);
    void setUndoSteps(UndoSteps steps);

    [[nodiscard]] const Action* action(const std::string& id) const;
    [[nodiscard]] const Setting* setting(const std::string& id) const;
    [[nodiscard]] const std::vector<Action>& actions() const { return actions_; }
    [[nodiscard]] const std::vector<Setting>& allSettings() const { return settings_; }
    [[nodiscard]] const std::vector<TrackType>& trackTypes() const { return trackTypes_; }
    [[nodiscard]] const TrackType* trackType(const std::string& kind) const;
    [[nodiscard]] const MidiDeviceProfile* midiDeviceProfile(const std::string& deviceName) const;
    [[nodiscard]] const std::vector<DocumentWarnings>& warnings() const { return warnings_; }
    [[nodiscard]] const std::vector<LoadingTracks>& loadingTracks() const { return loadingTracks_; }

    // Emits actionRan after an action that no other action ran, so the log shows what a person or agent asked for.
    Json run(const std::string& id, const Json& params = Json::object(),
             ActionSource source = ActionSource::Code) const;
    // Runs each {id, params} call in order as its own run, and stops at the first failure unless keepGoing. Each
    // entry of the result holds the call's result or its error.
    Json runBatch(const Json& calls, bool keepGoing, ActionSource source) const;
    // Runs call as a request from origin; inside an outer request, the outer origin stays.
    Json runFrom(ActionSource origin, const std::function<Json()>& call) const;
    [[nodiscard]] bool fromAgent() const { return origin_ == ActionSource::Mcp || origin_ == ActionSource::Lua; }
    [[nodiscard]] bool fromUi() const { return origin_ == ActionSource::Ui; }
    // Canonicalizes both roots, so a missing folder throws std::filesystem::filesystem_error.
    void setAgentRoots(AgentRoots roots);
    void setLimits(Limits limits) { limits_ = limits; }
    [[nodiscard]] const Limits& limits() const { return limits_; }
    // The limits and agent roots, as the limits state shows them.
    [[nodiscard]] Json limitsState() const;
    // Throws AppError when an offline render of that many seconds of audio runs past the render limit.
    void checkRenderSeconds(double seconds) const;
    // A headless run has no window, so run refuses the windowOnly actions.
    void setHeadless(bool headless) { headless_ = headless; }
    [[nodiscard]] bool headless() const { return headless_; }
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
    [[nodiscard]] static bool readOnly() { return false; }
    // Every action with its title, category and shortcut, for the key commands list.
    [[nodiscard]] QVariantList keyCommands() const;
    // The portable text of a key press, such as Ctrl+Shift+S, or empty for a modifier alone.
    Q_INVOKABLE static QString keySequence(int key, int modifiers);
    Q_INVOKABLE QVariantList palette(const QString& query) const;
    Q_INVOKABLE QVariantMap actionInfo(const QString& id) const;
    // Empty when the action marks no choice or its target is gone.
    Q_INVOKABLE QString currentChoice(const QString& id, const QVariantMap& params) const;
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
    // The path of a file URL, such as one dropped from the Finder, to pass as an action's path param.
    Q_INVOKABLE static QString localPath(const QUrl& file) { return file.toLocalFile(); }
    Q_INVOKABLE bool setSettingValue(const QString& id, const QVariant& value);

signals:
    void settingsChanged();
    void shortcutsChanged();
    void menusChanged();
    void fileDialogRequested(const QVariantMap& request);
    void confirmRequested(const QVariantMap& request);
    // issues are the result's issues, which the notice lists so a click can go to each one.
    void noticeRaised(const QString& title, const QString& text, const QVariantList& issues) const;
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
    std::vector<DocumentWarnings> warnings_;
    std::vector<LoadingTracks> loadingTracks_;
    // Runs the action and logs its failure, as every UI call does.
    std::optional<Json> runFromUi(const std::string& id, const Json& params) const;
    void runGesture();
    void setGesturing(bool value);

    UndoSteps undoSteps_;
    mutable int runDepth_ = 0;
    mutable std::optional<ActionSource> origin_;
    AgentRoots agentRoots_;
    Limits limits_;
    bool headless_ = false;
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

}  // namespace emerick::app
