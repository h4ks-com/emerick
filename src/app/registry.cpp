#include "emerick/app/registry.hpp"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeySequence>
#include <QRegularExpression>
#include <QtLogging>
#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

// A gesture updates the document at most once per 60 Hz frame.
constexpr auto kGestureFrame = std::chrono::milliseconds{16};

bool matchesType(const Setting& setting, const Json& value) {
    switch (setting.type) {
        case SettingType::Integer:
            return value.is_number_integer();
        case SettingType::Number:
            return value.is_number();
        case SettingType::Boolean:
            return value.is_boolean();
        case SettingType::Color: {
            static const QRegularExpression hex{"^#[0-9A-Fa-f]{6}$"};
            return value.is_string() && hex.match(qs(value.get<std::string>())).hasMatch();
        }
        case SettingType::Choice:
            return std::ranges::find(setting.choices, value) != setting.choices.end();
        case SettingType::String:
            return value.is_string();
        case SettingType::List:
            return value.is_array() && std::ranges::all_of(value, &Json::is_string);
        case SettingType::AudioRoute:
            return value.is_object() && value.contains("device") && value.at("device").is_string() &&
                   value.contains("channels") && value.at("channels").is_array() &&
                   std::ranges::all_of(value.at("channels"), &Json::is_number_integer);
    }
    return false;
}

bool inRange(const Setting& setting, const Json& value) {
    if (!value.is_number()) {
        return true;
    }
    const auto number = value.get<double>();
    return (!setting.minimum || number >= *setting.minimum) && (!setting.maximum || number <= *setting.maximum);
}

bool isValid(const Setting& setting, const Json& value) {
    return matchesType(setting, value) && inRange(setting, value);
}

Json defaultOf(const Setting& setting) { return setting.fallback ? setting.fallback() : setting.defaultValue; }

std::string expectation(const Setting& setting) {
    if (setting.type == SettingType::Choice) {
        return "one of " + setting.choices.dump();
    }
    std::string text = "a " + enumKey(setting.type);
    if (setting.minimum && setting.maximum) {
        text += " from " + Json(*setting.minimum).dump() + " to " + Json(*setting.maximum).dump();
    }
    return text;
}

// QML wants UTF-16 indices; the matcher reports UTF-8 byte offsets.
QVariantList utf16Ranges(const std::string& title, const std::vector<core::MatchRange>& ranges) {
    QVariantList result;
    for (const auto& range : ranges) {
        const auto begin = QString::fromUtf8(title.data(), static_cast<qsizetype>(range.begin)).size();
        const auto end = QString::fromUtf8(title.data(), static_cast<qsizetype>(range.end)).size();
        result.append(QVariant{QVariantList{begin, end}});
    }
    return result;
}

bool isEnabled(const Action& entry) { return !entry.enabled || entry.enabled(); }

class NestedRun {
public:
    NestedRun(int& depth, int step) : depth_(depth), step_(step) { depth_ += step_; }
    ~NestedRun() { depth_ -= step_; }
    NestedRun(const NestedRun&) = delete;
    NestedRun& operator=(const NestedRun&) = delete;
    NestedRun(NestedRun&&) = delete;
    NestedRun& operator=(NestedRun&&) = delete;

private:
    int& depth_;
    int step_;
};

class RequestOrigin {
public:
    RequestOrigin(std::optional<ActionSource>& origin, ActionSource source)
        : origin_(origin), outer_(origin.has_value()) {
        if (!outer_) {
            origin_ = source;
        }
    }
    ~RequestOrigin() {
        if (!outer_) {
            origin_.reset();
        }
    }
    RequestOrigin(const RequestOrigin&) = delete;
    RequestOrigin& operator=(const RequestOrigin&) = delete;
    RequestOrigin(RequestOrigin&&) = delete;
    RequestOrigin& operator=(RequestOrigin&&) = delete;

private:
    std::optional<ActionSource>& origin_;
    bool outer_;
};

struct BatchCall {
    std::string id;
    Json params;
};

std::vector<BatchCall> batchCalls(const Json& calls) {
    if (!calls.is_array()) {
        throw AppError("calls must be an array of {id, params}, got " + calls.dump());
    }
    if (calls.size() > kMaxBatchCalls) {
        throw AppError("a batch takes at most " + std::to_string(kMaxBatchCalls) + " calls, got " +
                       std::to_string(calls.size()) + "; split it into several batches");
    }
    return calls | std::views::transform([](const Json& call) {
               return BatchCall{param<std::string>(call, "id"),
                                optionalParam<Json>(call, "params").value_or(Json::object())};
           }) |
           std::ranges::to<std::vector>();
}

Json actionToJson(const Action& entry) {
    Json result{{"id", entry.id},
                {"title", entry.title},
                {"aliases", entry.aliases},
                {"keywords", entry.keywords},
                {"category", entry.category},
                {"enabled", isEnabled(entry)}};
    if (!entry.description.empty()) {
        result["description"] = entry.description;
    }
    if (!entry.shortcut.empty()) {
        result["shortcut"] = entry.shortcut;
    }
    if (!entry.schema.is_null()) {
        result["schema"] = entry.schema;
    }
    if (!entry.location.empty()) {
        result["location"] = entry.location;
    }
    return result;
}

Json optionalNumber(const std::optional<double>& value) { return value ? Json(*value) : Json(nullptr); }

bool inMenuBar(const std::string& menu) {
    const std::string top = menu.substr(0, menu.find('/'));
    return std::ranges::contains(kMenuBar, top, [](const char* name) { return std::string_view{name}; });
}

// Text fields need these keys, so the window binds them and turns them off while a text field has focus.
constexpr std::array kTextEditingKeys{"Ctrl+A",       "Ctrl+C",    "Ctrl+V",     "Ctrl+X",  "Ctrl+Z",
                                      "Ctrl+Shift+Z", "Ctrl+Left", "Ctrl+Right", "Ctrl+Up", "Ctrl+Down"};

bool menuBindsShortcut(const Action& entry) {
    const bool modified = entry.shortcut.starts_with("Ctrl+") || entry.shortcut.starts_with("Alt+");
    return modified && !std::ranges::contains(kTextEditingKeys, std::string_view{entry.shortcut}) &&
           std::ranges::any_of(entry.menus, inMenuBar);
}

QString shortcutKey(const std::string& id) { return "shortcuts/" + qs(id); }

std::string portableShortcut(const std::string& sequence) {
    return QKeySequence::fromString(qs(sequence), QKeySequence::PortableText)
        .toString(QKeySequence::PortableText)
        .toStdString();
}

// Actions run on the UI thread, so we show the wait cursor while one runs, since the window cannot answer meanwhile.
class WaitCursor {
public:
    explicit WaitCursor(bool wanted)
        : shown_(wanted && qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        if (shown_) {
            QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        }
    }
    ~WaitCursor() {
        if (shown_) {
            QGuiApplication::restoreOverrideCursor();
        }
    }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
    WaitCursor(WaitCursor&&) = delete;
    WaitCursor& operator=(WaitCursor&&) = delete;

private:
    bool shown_;
};

QVariantList menuChoices(const Action& action) {
    QVariantList choices;
    for (const MenuChoice& choice : action.submenu()) {
        choices.append(QVariantMap{{"id", qs(choice.id)},
                                   {"title", qs(choice.title)},
                                   {"params", toVariant(choice.params)},
                                   {"icon", qs(choice.icon)},
                                   {"color", choice.color}});
    }
    return choices;
}

}  // namespace

Registry::Registry(const QString& settingsPath, QObject* parent)
    : QObject(parent),
      store_(settingsPath.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(settingsPath, QSettings::IniFormat)) {
    gestureFrame_.setSingleShot(true);
    gestureFrame_.setInterval(kGestureFrame);
    connect(&gestureFrame_, &QTimer::timeout, this, [this] {
        if (gesturePending_) {
            runGesture();
        }
    });
}

void Registry::addAction(Action entry) {
    if (action(entry.id) != nullptr || setting(entry.id) != nullptr) {
        throw std::logic_error("duplicate registry id " + entry.id);
    }
    if (const QVariant stored = store_->value(shortcutKey(entry.id)); stored.isValid()) {
        entry.shortcut = stored.toString().toStdString();
    }
    actions_.push_back(std::move(entry));
    emit shortcutsChanged();
    emit menusChanged();
    emit settingsChanged();
}

void Registry::removeAction(const std::string& id) {
    if (std::erase_if(actions_, [&](const Action& entry) { return entry.id == id; }) > 0) {
        emit shortcutsChanged();
        emit menusChanged();
        emit settingsChanged();
    }
}

Json Registry::setShortcut(const std::string& id, const std::string& sequence) {
    const auto found = std::ranges::find(actions_, id, &Action::id);
    if (found == actions_.end()) {
        throw AppError("no action " + id + ", run actions.search for action ids");
    }
    const std::string shortcut = portableShortcut(sequence);
    if (shortcut.empty() && !sequence.empty()) {
        throw AppError(sequence + " is not a key sequence; write one like Ctrl+Shift+S");
    }
    Json replaced = nullptr;
    for (Action& other : actions_) {
        if (shortcut.empty() || other.id == id) {
            continue;
        }
        if (std::erase_if(other.extraShortcuts,
                          [&](const std::string& extra) { return portableShortcut(extra) == shortcut; }) > 0) {
            replaced = other.id;
        }
        if (portableShortcut(other.shortcut) == shortcut) {
            other.shortcut.clear();
            store_->setValue(shortcutKey(other.id), QString{});
            replaced = other.id;
        }
    }
    found->shortcut = shortcut;
    store_->setValue(shortcutKey(id), qs(shortcut));
    emit shortcutsChanged();
    emit menusChanged();
    emit settingsChanged();
    return {{"id", id}, {"shortcut", shortcut}, {"replaced", replaced}};
}

QVariantList Registry::keyCommands() const {
    return actions_ | std::views::transform([](const Action& entry) {
               return QVariant{QVariantMap{{"id", qs(entry.id)},
                                           {"title", qs(entry.title)},
                                           {"category", qs(entry.category)},
                                           {"shortcut", qs(entry.shortcut)},
                                           {"extraShortcuts", entry.extraShortcuts | std::views::transform(qs) |
                                                                  std::ranges::to<QStringList>()}}};
           }) |
           std::ranges::to<QVariantList>();
}

QString Registry::keySequence(int key, int modifiers) {
    constexpr std::array kModifierKeys{Qt::Key_Shift, Qt::Key_Control, Qt::Key_Meta, Qt::Key_Alt, Qt::Key_unknown};
    if (std::ranges::contains(kModifierKeys, static_cast<Qt::Key>(key))) {
        return {};
    }
    const auto held = Qt::KeyboardModifiers::fromInt(static_cast<unsigned int>(modifiers)) & ~Qt::KeypadModifier;
    return QKeySequence{QKeyCombination{held, static_cast<Qt::Key>(key)}}.toString(QKeySequence::PortableText);
}

void Registry::addSetting(Setting entry) {
    if (action(entry.id) != nullptr || setting(entry.id) != nullptr) {
        throw std::logic_error("duplicate registry id " + entry.id);
    }
    settings_.push_back(std::move(entry));
    emit settingsChanged();
}

void Registry::addTrackType(TrackType type) {
    if (trackType(type.kind) != nullptr) {
        throw std::logic_error("duplicate track kind " + type.kind);
    }
    trackTypes_.push_back(std::move(type));
}

void Registry::addMidiDeviceProfile(MidiDeviceProfile profile) { midiDeviceProfiles_.push_back(std::move(profile)); }

void Registry::addLocation(Location location) {
    if (std::ranges::contains(locations_, location.id, &Location::id)) {
        throw std::logic_error("duplicate location " + location.id);
    }
    locations_.push_back(std::move(location));
}

void Registry::addStateProvider(const std::string& name, std::function<Json()> provider) {
    stateProviders_[name] = std::move(provider);
}

void Registry::addWarnings(DocumentWarnings warnings) { warnings_.push_back(std::move(warnings)); }

void Registry::addLoadingTracks(LoadingTracks loading) { loadingTracks_.push_back(std::move(loading)); }

const Action* Registry::action(const std::string& id) const {
    const auto found = std::ranges::find(actions_, id, &Action::id);
    return found == actions_.end() ? nullptr : &*found;
}

const Setting* Registry::setting(const std::string& id) const {
    const auto found = std::ranges::find(settings_, id, &Setting::id);
    return found == settings_.end() ? nullptr : &*found;
}

const TrackType* Registry::trackType(const std::string& kind) const {
    const auto found = std::ranges::find(trackTypes_, kind, &TrackType::kind);
    return found == trackTypes_.end() ? nullptr : &*found;
}

const MidiDeviceProfile* Registry::midiDeviceProfile(const std::string& deviceName) const {
    const auto found = std::ranges::find_if(
        midiDeviceProfiles_, [&](const MidiDeviceProfile& profile) { return deviceName.contains(profile.nameMatch); });
    return found == midiDeviceProfiles_.end() ? nullptr : &*found;
}

Json Registry::run(const std::string& id, const Json& params, ActionSource source) const {
    const Action* found = action(id);
    if (found == nullptr) {
        throw AppError("no action " + id);
    }
    if (!isEnabled(*found)) {
        throw AppError("action " + id + " is disabled");
    }
    if (headless_ && found->windowOnly) {
        throw AppError("action " + id + " needs a window, and this run is headless");
    }
    const Json given = params.is_null() ? Json::object() : params;
    const RequestOrigin origin{origin_, source};
    const bool logged = runDepth_ == 0 && !found->quiet;
    // We copy the handler, because a script action can add or remove actions while it runs.
    const auto handler = found->run;
    Json result;
    {
        const NestedRun nested{runDepth_, found->quiet ? 0 : 1};
        const auto runHandler = [&] { result = handler(given); };
        try {
            if (undoSteps_.atomic) {
                undoSteps_.atomic(runHandler);
            } else {
                runHandler();
            }
        } catch (const std::filesystem::filesystem_error& error) {
            throw AppError(error.what());
        }
    }
    if (logged) {
        emit actionRan(id, given, source);
    }
    return result.is_null() ? Json::object() : result;
}

Json Registry::runBatch(const Json& calls, bool keepGoing, ActionSource source) const {
    Json results = Json::array();
    for (const BatchCall& call : batchCalls(calls)) {
        std::optional<std::string> error;
        try {
            results.push_back({{"id", call.id}, {"result", run(call.id, call.params, source)}});
        } catch (const AppError& failure) {
            error = failure.what();
        } catch (const Json::exception& failure) {
            error = failure.what();
        }
        if (error) {
            results.push_back({{"id", call.id}, {"error", *error}});
            if (!keepGoing) {
                break;
            }
        }
    }
    return {{"results", results}};
}

Json Registry::runFrom(ActionSource origin, const std::function<Json()>& call) const {
    const RequestOrigin scope{origin_, origin};
    return call();
}

void Registry::setAgentRoots(AgentRoots roots) {
    for (std::optional<std::filesystem::path>* root : {&roots.read, &roots.write}) {
        if (*root) {
            *root = std::filesystem::canonical(**root);
        }
    }
    agentRoots_ = std::move(roots);
}

Json Registry::limitsState() const {
    const auto optionalJson = [](const auto& value, const auto& convert) {
        return value ? Json(convert(*value)) : Json(nullptr);
    };
    const auto pathText = [](const std::filesystem::path& path) { return path.string(); };
    return {{"luaTimeoutMs", optionalJson(limits_.luaTimeout, [](auto timeout) { return timeout.count(); })},
            {"luaMemoryMb", limits_.luaMemoryBytes >> 20},
            {"maxRenderSeconds", optionalJson(limits_.renderSeconds, [](double seconds) { return seconds; })},
            {"bundledPluginsOnly", limits_.bundledPluginsOnly},
            {"libraryInstall", limits_.libraryInstall},
            {"agentReadRoot", optionalJson(agentRoots_.read, pathText)},
            {"agentWriteRoot", optionalJson(agentRoots_.write, pathText)}};
}

void Registry::checkRenderSeconds(double seconds) const {
    if (limits_.renderSeconds && seconds > *limits_.renderSeconds) {
        throw AppError(
            std::format("the render would run {:.1f} s of audio, over the limit of {:g} s; render a "
                        "shorter range or project",
                        seconds, *limits_.renderSeconds));
    }
}

std::filesystem::path Registry::checkedPath(const std::string& path, PathAccess access) const {
    const std::optional<std::filesystem::path>& root =
        access == PathAccess::Read ? agentRoots_.read : agentRoots_.write;
    if (!fromAgent() || !root) {
        return path;
    }
    if (auto inside = pathInside(path, *root)) {
        return *std::move(inside);
    }
    throw AppError(path + " is outside " + root->string() + ", the folder agents may " +
                   (access == PathAccess::Read ? "read" : "write"));
}

Json Registry::settingValue(const std::string& id) const {
    const Setting* found = setting(id);
    if (found == nullptr) {
        throw AppError("no setting " + id);
    }
    if (found->value) {
        return found->value();
    }
    const QVariant stored = store_->value(qs(id));
    if (!stored.isValid()) {
        return defaultOf(*found);
    }
    Json value = Json::parse(stored.toString().toStdString(), nullptr, false);
    return value.is_discarded() || !isValid(*found, value) ? defaultOf(*found) : value;
}

Json Registry::setSetting(const std::string& id, const Json& given) {
    const Setting* found = setting(id);
    if (found == nullptr) {
        throw AppError("no setting " + id);
    }
    if (found->readOnly) {
        throw AppError("setting " + id + " is read only");
    }
    const Json value = found->merge ? found->merge(settingValue(id), given) : given;
    if (!isValid(*found, value)) {
        throw AppError("setting " + id + " expects " + expectation(*found) + ", got " + value.dump());
    }
    store_->setValue(qs(id), qs(value.dump()));
    emit settingsChanged();
    return value;
}

Json Registry::state(const std::string& name) const {
    const auto found = stateProviders_.find(name);
    if (found == stateProviders_.end()) {
        throw AppError("no state provider " + name);
    }
    return found->second();
}

Json Registry::allState() const {
    Json result = Json::object();
    for (const auto& [name, provider] : stateProviders_) {
        result[name] = provider();
    }
    return result;
}

std::vector<SearchHit> Registry::search(const std::string& query, std::size_t limit) const {
    std::vector<SearchHit> hits;
    const auto consider = [&](EntryKind kind, const auto& entry, const std::vector<std::string>& keywords,
                              std::string shortcut) {
        std::vector<core::FuzzyField> fields{{entry.title, 1.0}};
        for (const auto& alias : entry.aliases) {
            fields.push_back({alias, 0.9});
        }
        fields.push_back({entry.id, 0.7});
        for (const auto& keyword : keywords) {
            fields.push_back({keyword, 0.6});
        }
        fields.push_back({entry.category, 0.3});
        if (auto match = core::fuzzyMatchFields(query, fields)) {
            hits.push_back(
                {kind, entry.id, entry.title, entry.category, std::move(shortcut), entry.location, std::move(*match)});
        }
    };
    for (const Action& entry : actions_) {
        consider(EntryKind::Action, entry, entry.keywords, entry.shortcut);
    }
    for (const Setting& entry : settings_) {
        consider(EntryKind::Setting, entry, {}, {});
    }
    // Equal scores keep the order actions register in, which follows the menus, so a menu's main entries come first.
    std::ranges::stable_sort(hits, std::ranges::greater{}, [](const SearchHit& hit) { return hit.match.score; });
    hits.resize(std::min(hits.size(), limit));
    return hits;
}

Json Registry::describeActions(const std::string& query) const {
    Json result = Json::array();
    for (const SearchHit& hit : search(query, actions_.size() + settings_.size())) {
        if (hit.kind == EntryKind::Action) {
            result.push_back(actionToJson(*action(hit.id)));
        }
    }
    return result;
}

Json Registry::describeSetting(const std::string& id) const {
    const Setting* found = setting(id);
    if (found == nullptr) {
        throw AppError("no setting " + id);
    }
    return {{"id", found->id},
            {"title", found->title},
            {"type", enumKey(found->type)},
            {"value", settingValue(found->id)},
            {"default", defaultOf(*found)},
            {"choices", found->choices},
            {"minimum", optionalNumber(found->minimum)},
            {"maximum", optionalNumber(found->maximum)},
            {"readOnly", found->readOnly},
            {"location", found->location}};
}

Json Registry::describeSettings() const {
    return settings_ | std::views::transform([this](const Setting& entry) { return describeSetting(entry.id); }) |
           std::ranges::to<Json::array_t>();
}

Revealed Registry::reveal(const std::string& id) {
    std::string locationId = id;
    if (!std::ranges::contains(locations_, id, &Location::id)) {
        const Action* foundAction = action(id);
        const Setting* foundSetting = setting(id);
        if (foundAction == nullptr && foundSetting == nullptr) {
            throw AppError("no location, action or setting " + id);
        }
        locationId = foundAction != nullptr ? foundAction->location : foundSetting->location;
        if (locationId.empty()) {
            throw AppError(id + " has no UI location");
        }
    }
    Revealed revealed{id, {}};
    std::vector<const Location*> steps;
    for (std::string step = locationId; !step.empty();) {
        const auto found = std::ranges::find(locations_, step, &Location::id);
        if (found == locations_.end()) {
            throw AppError("no location " + step);
        }
        steps.push_back(&*found);
        step = found->parent;
    }
    std::ranges::reverse(steps);
    for (const Location* step : steps) {
        revealed.chain.push_back(step->id);
        step->reveal();
    }
    return revealed;
}

QVariantMap Registry::settingsMap() const {
    QVariantMap result;
    for (const Setting& entry : settings_) {
        result.insert(qs(entry.id), toVariant(settingValue(entry.id)));
    }
    return result;
}

QVariantList Registry::shortcuts() const {
    QVariantList result;
    for (const Action& entry : actions_) {
        if (!entry.shortcut.empty() && !menuBindsShortcut(entry)) {
            result.append(QVariantMap{{"id", qs(entry.id)}, {"shortcut", qs(entry.shortcut)}});
        }
        for (const std::string& extra : entry.extraShortcuts) {
            result.append(QVariantMap{{"id", qs(entry.id)}, {"shortcut", qs(extra)}});
        }
    }
    return result;
}

QStringList Registry::menuBar() const {
    const QVariantMap all = menus();
    return kMenuBar | std::views::filter([&](const char* menu) { return all.contains(menu); }) |
           std::views::transform([](const char* menu) { return QString::fromUtf8(menu); }) |
           std::ranges::to<QStringList>();
}

QVariantMap Registry::menus() const {
    QVariantMap result;
    const auto append = [&result](const QString& menu, const QVariantMap& item) {
        QVariantList items = result.value(menu).toList();
        items.append(item);
        result.insert(menu, items);
    };
    for (const Action& entry : actions_) {
        QVariantMap item{{"id", qs(entry.id)},
                         {"title", qs(entry.title)},
                         {"shortcut", qs(entry.shortcut)},
                         {"enabled", isEnabled(entry)},
                         {"row", static_cast<int>(entry.submenu ? MenuRow::Submenu : MenuRow::Command)}};
        if (entry.submenu) {
            item.insert("choices", menuChoices(entry));
        }
        for (const std::string& menu : entry.menus) {
            item.insert("bindsShortcut", inMenuBar(menu) && menuBindsShortcut(entry));
            const auto slash = menu.rfind('/');
            if (slash != std::string::npos && !result.contains(qs(menu))) {
                append(qs(menu.substr(0, slash)), {{"id", qs(menu)},
                                                   {"title", qs(menu.substr(slash + 1))},
                                                   {"row", static_cast<int>(MenuRow::Menu)}});
            }
            append(qs(menu), item);
        }
    }
    return result;
}

QVariantList Registry::palette(const QString& query) const {
    QVariantList result;
    for (const SearchHit& hit : search(query.toStdString())) {
        const Action* entry = action(hit.id);
        // A submenu action needs one of its choices, which only a menu offers.
        if (entry != nullptr && entry->submenu) {
            continue;
        }
        result.append(QVariantMap{{"id", qs(hit.id)},
                                  {"title", qs(hit.title)},
                                  {"category", qs(hit.category)},
                                  {"shortcut", qs(hit.shortcut)},
                                  {"enabled", entry == nullptr || isEnabled(*entry)},
                                  {"ranges", utf16Ranges(hit.title, hit.match.ranges)}});
    }
    return result;
}

QVariantMap Registry::actionInfo(const QString& id) const {
    const Action* found = action(id.toStdString());
    if (found == nullptr) {
        qWarning() << "no action" << id;
        return {};
    }
    QVariantMap info{{"title", qs(found->title)}, {"shortcut", qs(found->shortcut)}, {"enabled", isEnabled(*found)}};
    if (found->submenu) {
        info.insert("choices", menuChoices(*found));
    }
    return info;
}

QString Registry::currentChoice(const QString& id, const QVariantMap& params) const {
    const Action* found = action(id.toStdString());
    if (found == nullptr || !found->current) {
        return {};
    }
    try {
        return qs(found->current(toJson(QVariant{params})));
    } catch (const AppError&) {
        return {};
    }
}

QVariantList Registry::sections(const QString& parent) const {
    auto children =
        locations_ | std::views::filter([&](const Location& entry) { return entry.parent == parent.toStdString(); }) |
        std::views::transform([](const Location& entry) { return &entry; }) | std::ranges::to<std::vector>();
    std::ranges::stable_sort(children, {}, [](const Location* entry) { return !entry->group.empty(); });
    QVariantList result;
    for (const Location* location : children) {
        result.append(QVariantMap{{"id", qs(location->id)},
                                  {"key", qs(location->id.substr(location->parent.size() + 1))},
                                  {"title", qs(location->title)},
                                  {"group", qs(location->group)}});
    }
    return result;
}

QVariantList Registry::settingsAt(const QString& location) const {
    QVariantList result;
    for (const Setting& entry : settings_ | std::views::filter([&](const Setting& candidate) {
                                    return candidate.location == location.toStdString();
                                })) {
        result.append(QVariantMap{{"id", qs(entry.id)},
                                  {"title", qs(entry.title)},
                                  {"type", static_cast<int>(entry.type)},
                                  {"choices", toVariant(entry.choices)},
                                  {"minimum", entry.minimum.value_or(std::numeric_limits<int>::min())},
                                  {"maximum", entry.maximum.value_or(std::numeric_limits<int>::max())},
                                  {"readOnly", entry.readOnly},
                                  {"hint", qs(entry.hint)},
                                  {"action", qs(entry.action)},
                                  {"suggestions", toVariant(entry.suggestions ? entry.suggestions() : Json::array())}});
    }
    return result;
}

void Registry::activate(const QString& id) {
    const Action* found = action(id.toStdString());
    if (found != nullptr && found->dialog) {
        emit fileDialogRequested({{"id", id},
                                  {"mode", static_cast<int>(found->dialog->mode)},
                                  {"title", qs(found->title)},
                                  {"nameFilter", qs(found->dialog->nameFilter)},
                                  {"suffix", qs(found->dialog->suffix)}});
        return;
    }
    if (found != nullptr && !found->confirm.empty()) {
        emit confirmRequested({{"id", id}, {"title", qs(found->title)}, {"text", qs(found->confirm)}});
        return;
    }
    if (found != nullptr) {
        runFromUi(id.toStdString(), Json::object());
        return;
    }
    try {
        reveal(id.toStdString());
    } catch (const AppError& error) {
        qWarning() << error.what();
    }
}

std::optional<Json> Registry::runFromUi(const std::string& id, const Json& params) const {
    const Action* found = action(id);
    const auto notify = [&](const QString& text) {
        if (found != nullptr && found->notifies) {
            emit noticeRaised(qs(found->title), text);
        }
    };
    const WaitCursor waitCursor{!gesturing_};
    try {
        Json result = run(id, params, ActionSource::Ui);
        notify(qs(result.value("message", std::string{})));
        return result;
    } catch (const AppError& error) {
        qWarning() << error.what();
        notify(qs(error.what()));
    } catch (const Json::exception& error) {
        qWarning() << error.what();
        notify(qs(error.what()));
    }
    return std::nullopt;
}

QVariant Registry::runAction(const QString& id, const QVariantMap& params) const {
    const std::optional<Json> result = runFromUi(id.toStdString(), toJson(QVariant{params}));
    return result ? toVariant(*result) : QVariant{};
}

void Registry::setUndoSteps(UndoSteps steps) { undoSteps_ = std::move(steps); }

void Registry::updateGesture(const QString& name, const QVariantList& calls) {
    if (gestureCancelled_) {
        return;
    }
    gestureName_ = name.toStdString();
    gestureCalls_ = calls | std::views::transform([](const QVariant& call) {
                        const QVariantMap entry = call.toMap();
                        return Call{entry.value("id").toString().toStdString(), toJson(entry.value("params"))};
                    }) |
                    std::ranges::to<std::vector>();
    gesturePending_ = true;
    setGesturing(true);
    if (!gestureFrame_.isActive()) {
        runGesture();
    }
}

void Registry::runGesture() {
    gesturePending_ = false;
    const auto runAll = [this] {
        const NestedRun nested{runDepth_, 1};
        for (const Call& call : gestureCalls_) {
            runFromUi(call.id, call.params);
        }
    };
    if (undoSteps_.updateGesture) {
        undoSteps_.updateGesture(gestureName_, runAll);
    } else {
        runAll();
    }
    gestureFrame_.start();
}

void Registry::endGesture() {
    if (gesturePending_) {
        runGesture();
    }
    gestureFrame_.stop();
    gestureCancelled_ = false;
    if (!gesturing_) {
        return;
    }
    if (!undoSteps_.endGesture || undoSteps_.endGesture()) {
        for (const Call& call : gestureCalls_) {
            emit actionRan(call.id, call.params, ActionSource::Ui);
        }
    }
    gestureCalls_.clear();
    setGesturing(false);
}

void Registry::cancelGesture() {
    gestureFrame_.stop();
    gesturePending_ = false;
    gestureCalls_.clear();
    if (gesturing_) {
        gestureCancelled_ = true;
        if (undoSteps_.cancelGesture) {
            undoSteps_.cancelGesture();
        }
        setGesturing(false);
    }
}

void Registry::setGesturing(bool value) {
    if (std::exchange(gesturing_, value) != value) {
        emit gesturingChanged();
    }
}

void Registry::runActions(const QString& name, const QVariantList& calls) const {
    const auto runAll = [&] {
        for (const QVariant& call : calls) {
            const QVariantMap entry = call.toMap();
            runAction(entry.value("id").toString(), entry.value("params").toMap());
        }
    };
    if (undoSteps_.group) {
        undoSteps_.group(name.toStdString(), runAll);
    } else {
        runAll();
    }
}

QVariant Registry::runWithFile(const QString& id, const QUrl& file) const {
    const Action* found = action(id.toStdString());
    if (found == nullptr || !found->dialog) {
        qWarning() << "action" << id << "has no file dialog";
        return {};
    }
    return runAction(id, {{qs(found->dialog->param), file.toLocalFile()}});
}

bool Registry::setSettingValue(const QString& id, const QVariant& value) {
    try {
        setSetting(id.toStdString(), toJson(value));
        return true;
    } catch (const AppError& error) {
        qWarning() << error.what();
        return false;
    }
}

}  // namespace emerick::app
