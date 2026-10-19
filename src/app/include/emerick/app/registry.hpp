#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QUrl>
#include <QVariant>
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

// The menu bar's File menu binds the shortcuts of its actions, so the window skips them.
inline constexpr const char* kFileMenu = "File";
// The menu behind the document button in the header.
inline constexpr const char* kProjectMenu = "Project";

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
    std::vector<std::string> aliases = {};
    std::vector<std::string> keywords = {};
    std::string category = {};
    std::string shortcut = {};
    std::function<bool()> enabled = {};
    Json schema = {};
    std::function<Json(const Json& params)> run = {};
    std::string location = {};
    std::vector<std::string> menus = {};
    std::optional<FileDialog> dialog = {};
    // A UI activation asks this question first and runs the action with confirm: true.
    std::string confirm = {};
    std::function<std::vector<MenuChoice>()> submenu = {};
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
    Q_PROPERTY(QVariantList shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(QVariantMap menus READ menus NOTIFY settingsChanged)

public:
    // An empty path uses the platform's default QSettings store.
    explicit Registry(const QString& settingsPath = {}, QObject* parent = nullptr);

    void addAction(Action entry);
    void addSetting(Setting entry);
    void addTrackType(TrackType type);
    void addMidiDeviceProfile(MidiDeviceProfile profile);
    void addLocation(Location location);
    void addStateProvider(const std::string& name, std::function<Json()> provider);

    [[nodiscard]] const Action* action(const std::string& id) const;
    [[nodiscard]] const Setting* setting(const std::string& id) const;
    [[nodiscard]] const std::vector<Action>& actions() const { return actions_; }
    [[nodiscard]] const std::vector<Setting>& allSettings() const { return settings_; }
    [[nodiscard]] const std::vector<TrackType>& trackTypes() const { return trackTypes_; }
    [[nodiscard]] const TrackType* trackType(const std::string& kind) const;
    [[nodiscard]] const MidiDeviceProfile* midiDeviceProfile(const std::string& deviceName) const;

    Json run(const std::string& id, const Json& params = Json::object()) const;
    [[nodiscard]] Json settingValue(const std::string& id) const;
    Json setSetting(const std::string& id, const Json& value);
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
    // Each menu's items in registration order. Only File menu items bind their shortcut.
    [[nodiscard]] QVariantMap menus() const;
    Q_INVOKABLE QVariantList palette(const QString& query) const;
    Q_INVOKABLE QVariantMap actionInfo(const QString& id) const;
    Q_INVOKABLE QVariantList sections(const QString& parent) const;
    Q_INVOKABLE QVariantList settingsAt(const QString& location) const;
    // The UI runs actions through here. An action with a file dialog or a confirm question asks the user first and
    // runs from the answer.
    Q_INVOKABLE void activate(const QString& id);
    // QML gets an empty value for a failed action; we log the failure.
    Q_INVOKABLE QVariant runAction(const QString& id, const QVariantMap& params) const;
    // Runs the action with the file a dialog picked as its dialog param.
    Q_INVOKABLE QVariant runWithFile(const QString& id, const QUrl& file) const;
    Q_INVOKABLE bool setSettingValue(const QString& id, const QVariant& value);

signals:
    void settingsChanged();
    void fileDialogRequested(const QVariantMap& request);
    void confirmRequested(const QVariantMap& request);

private:
    std::unique_ptr<QSettings> store_;
    std::vector<Action> actions_;
    std::vector<Setting> settings_;
    std::vector<TrackType> trackTypes_;
    std::vector<MidiDeviceProfile> midiDeviceProfiles_;
    std::vector<Location> locations_;
    std::map<std::string, std::function<Json()>> stateProviders_;
};

QString qs(const std::string& text);
QVariant toVariant(const Json& value);
Json toJson(const QVariant& value);

}  // namespace emerick::app
