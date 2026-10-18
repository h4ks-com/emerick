#include "emerick/app/registry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtLogging>
#include <algorithm>
#include <limits>
#include <ranges>

namespace emerick::app {

namespace {

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

Json actionToJson(const Action& entry) {
    Json result{{"id", entry.id},
                {"title", entry.title},
                {"aliases", entry.aliases},
                {"keywords", entry.keywords},
                {"category", entry.category},
                {"enabled", isEnabled(entry)}};
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

}  // namespace

QString qs(const std::string& text) { return QString::fromStdString(text); }

QVariant toVariant(const Json& value) {
    const auto wrapped = QJsonDocument::fromJson(QByteArray::fromStdString(Json::array({value}).dump()));
    return wrapped.array().at(0).toVariant();
}

Json toJson(const QVariant& value) {
    const QJsonDocument wrapped{QJsonArray{QJsonValue::fromVariant(value)}};
    return Json::parse(wrapped.toJson(QJsonDocument::Compact).toStdString()).at(0);
}

Registry::Registry(const QString& settingsPath, QObject* parent)
    : QObject(parent),
      store_(settingsPath.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(settingsPath, QSettings::IniFormat)) {}

void Registry::addAction(Action entry) {
    if (action(entry.id) != nullptr || setting(entry.id) != nullptr) {
        throw std::logic_error("duplicate registry id " + entry.id);
    }
    actions_.push_back(std::move(entry));
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

Json Registry::run(const std::string& id, const Json& params) const {
    const Action* found = action(id);
    if (found == nullptr) {
        throw AppError("no action " + id);
    }
    if (!isEnabled(*found)) {
        throw AppError("action " + id + " is disabled");
    }
    Json result = found->run(params.is_null() ? Json::object() : params);
    return result.is_null() ? Json::object() : result;
}

Json Registry::settingValue(const std::string& id) const {
    const Setting* found = setting(id);
    if (found == nullptr) {
        throw AppError("no setting " + id);
    }
    const QVariant stored = store_->value(qs(id));
    if (!stored.isValid()) {
        return found->defaultValue;
    }
    Json value = Json::parse(stored.toString().toStdString(), nullptr, false);
    return value.is_discarded() || !isValid(*found, value) ? found->defaultValue : value;
}

Json Registry::setSetting(const std::string& id, const Json& value) {
    const Setting* found = setting(id);
    if (found == nullptr) {
        throw AppError("no setting " + id);
    }
    if (found->readOnly) {
        throw AppError("setting " + id + " is read only");
    }
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
    std::ranges::sort(hits, [](const SearchHit& a, const SearchHit& b) {
        if (a.match.score != b.match.score) {
            return a.match.score > b.match.score;
        }
        return a.title.size() != b.title.size() ? a.title.size() < b.title.size() : a.id < b.id;
    });
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
            {"default", found->defaultValue},
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
    for (const Action& entry : actions_ | std::views::filter([](const Action& a) {
                                   return !a.shortcut.empty() && !std::ranges::contains(a.menus, kFileMenu);
                               })) {
        result.append(QVariantMap{{"id", qs(entry.id)}, {"shortcut", qs(entry.shortcut)}});
    }
    return result;
}

QVariantMap Registry::menus() const {
    QVariantMap result;
    for (const Action& entry : actions_) {
        QVariantMap item{{"id", qs(entry.id)},
                         {"title", qs(entry.title)},
                         {"shortcut", qs(entry.shortcut)},
                         {"enabled", isEnabled(entry)},
                         {"row", static_cast<int>(entry.submenu ? MenuRow::Submenu : MenuRow::Command)}};
        if (entry.submenu) {
            QVariantList choices;
            for (const MenuChoice& choice : entry.submenu()) {
                choices.append(QVariantMap{
                    {"id", qs(choice.id)}, {"title", qs(choice.title)}, {"params", toVariant(choice.params)}});
            }
            item.insert("choices", choices);
        }
        for (const std::string& menu : entry.menus) {
            item.insert("bindsShortcut", menu == kFileMenu);
            QVariantList items = result.value(qs(menu)).toList();
            items.append(item);
            result.insert(qs(menu), items);
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
    return {{"title", qs(found->title)}, {"shortcut", qs(found->shortcut)}};
}

QVariantList Registry::sections(const QString& parent) const {
    QVariantList result;
    for (const Location& location :
         locations_ | std::views::filter([&](const Location& entry) { return entry.parent == parent.toStdString(); })) {
        result.append(QVariantMap{{"id", qs(location.id)},
                                  {"key", qs(location.id.substr(location.parent.size() + 1))},
                                  {"title", qs(location.title)}});
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
                                  {"hint", qs(entry.hint)}});
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
    try {
        if (found != nullptr) {
            run(id.toStdString());
        } else {
            reveal(id.toStdString());
        }
    } catch (const AppError& error) {
        qWarning() << error.what();
    }
}

QVariant Registry::runAction(const QString& id, const QVariantMap& params) const {
    try {
        return toVariant(run(id.toStdString(), toJson(QVariant{params})));
    } catch (const AppError& error) {
        qWarning() << error.what();
    } catch (const Json::exception& error) {
        qWarning() << error.what();
    }
    return {};
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
