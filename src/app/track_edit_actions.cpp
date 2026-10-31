#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/automation_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

struct TrackFlag {
    std::string_view id;
    std::string_view title;
    std::string_view alias;
    std::string_view shortcut;
    bool model::Track::* field;
    void (ProjectController::*live)(const model::TrackId&, bool);
};

constexpr std::array kTrackFlags{
    TrackFlag{"track.setMute", "Mute track", "silence track", "M", &model::Track::mute,
              &ProjectController::setLiveMute},
    TrackFlag{"track.setSolo", "Solo track", "listen alone", "S", &model::Track::solo, &ProjectController::setLiveSolo},
    TrackFlag{"track.setArm", "Arm track for recording", "record enable", "Alt+R", &model::Track::armed,
              &ProjectController::setLiveArmed},
    TrackFlag{"track.setMonitor", "Monitor track input", "input monitoring", "", &model::Track::monitor, nullptr}};

constexpr double kMaxRecordDelayMs = 1000.0;
constexpr double kMaxTrimDb = 24.0;

constexpr std::array<std::string_view, kTrackColors> kColorNames{"Teal",  "Blue",  "Violet", "Rose",
                                                                 "Amber", "Green", "Sand",   "Steel"};

struct TrackIcon {
    std::string_view icon;
    std::string_view title;
};

constexpr std::array kTrackIcons{TrackIcon{"waveform", "Waveform"}, TrackIcon{"piano-keys", "Piano keys"},
                                 TrackIcon{"guitar", "Guitar"},     TrackIcon{"music-note", "Note"},
                                 TrackIcon{"music-notes", "Notes"}, TrackIcon{"squares-four", "Pads"},
                                 TrackIcon{"circle", "Circle"}};

std::vector<std::string> trackMenus() { return {kTrackMenu, kTrackContextMenu}; }

std::size_t trackIndex(const ProjectController& controller, const model::TrackId& id) {
    const auto& tracks = controller.project().document().tracks;
    return static_cast<std::size_t>(std::ranges::find(tracks, id, &model::Track::id) - tracks.begin());
}

model::Track duplicateTrack(const model::Track& track) {
    model::Track copy = track;
    copy.id = model::TrackId::make();
    copy.name = track.name + " copy";
    copy.armed = false;
    copy.clips = track.clips | std::views::transform([](const model::Clip& clip) {
                     return model::duplicateClip(clip, model::clipStart(clip));
                 }) |
                 std::ranges::to<std::vector>();
    for (model::Insert& insert : copy.inserts) {
        insert.id = model::InsertId::make();
    }
    return copy;
}

Json selectNeighbor(ProjectController& controller, Nav& nav, std::ptrdiff_t step) {
    const auto& tracks = controller.project().document().tracks;
    if (tracks.empty()) {
        throw AppError("add a track first");
    }
    const auto current = std::ranges::find(tracks, nav.selectedTrack().toStdString(),
                                           [](const model::Track& track) { return track.id.value; });
    const std::ptrdiff_t last = std::ssize(tracks) - 1;
    // Without a selected track, Down picks the first track and Up the last.
    const std::ptrdiff_t unselected = step > 0 ? -1 : last + 1;
    const std::ptrdiff_t from = current == tracks.end() ? unselected : current - tracks.begin();
    const std::ptrdiff_t at = std::clamp<std::ptrdiff_t>(from + step, 0, last);
    const model::Track& track = tracks[static_cast<std::size_t>(at)];
    nav.setSelectedTrack(qs(track.id.value));
    nav.setSelectedClips({});
    return trackResult(controller, track.id);
}

// The track joins the selection and shows in the inspector, or leaves it when it was selected already.
void toggleSelected(Nav& nav, const QString& id) {
    QStringList selected = nav.selectedTracks();
    if (selected.removeAll(id) > 0) {
        nav.setSelectedTracks(selected);
    } else {
        nav.selectTracks(selected << id, id);
    }
}

// Selects every track from the selected track to the given one, which the inspector then shows.
void selectRange(const ProjectController& controller, Nav& nav, const model::TrackId& to) {
    const auto& tracks = controller.project().document().tracks;
    const auto rowOf = [&](const std::string& id) {
        return std::ranges::find(tracks, id, [](const model::Track& track) { return track.id.value; }) - tracks.begin();
    };
    const std::ptrdiff_t end = rowOf(to.value);
    const std::ptrdiff_t anchor = rowOf(nav.selectedTrack().toStdString());
    const auto [first, last] = std::minmax(anchor == std::ssize(tracks) ? end : anchor, end);
    nav.selectTracks(tracks | std::views::drop(first) | std::views::take(last - first + 1) |
                         std::views::transform([](const model::Track& track) { return qs(track.id.value); }) |
                         std::ranges::to<QStringList>(),
                     qs(to.value));
}

void registerTrackFlags(Registry& registry, ProjectController& controller, const Nav& nav) {
    for (const TrackFlag& flag : kTrackFlags) {
        registry.addAction(
            {.id = std::string{flag.id},
             .title = std::string{flag.title},
             .aliases = {std::string{flag.alias}},
             .category = "Tracks",
             .shortcut = std::string{flag.shortcut},
             .schema = {{"properties",
                         {{"track", {{"type", "string"}}},
                          {"value", {{"type", "boolean"}}},
                          {"selection", {{"type", "boolean"}}}}}},
             .run =
                 [&controller, &nav, flag](const Json& params) {
                     const auto target = targetTrack(controller, nav, params);
                     const bool value = optionalParam<bool>(params, "value")
                                            .value_or(!(controller.project().track(target)->*flag.field));
                     return editTargets(controller, nav, params, std::string{flag.title},
                                        [&](const model::TrackId& id) {
                                            editTrack(controller, id, std::string{flag.title},
                                                      [&](model::Track& track) { track.*flag.field = value; });
                                            if (flag.live != nullptr) {
                                                (controller.*flag.live)(id, value);
                                            }
                                        });
                 },
             .menus = trackMenus()});
    }
    registry.addAction(
        {.id = "track.armAll",
         .title = "Arm all tracks",
         .description = "Arms every track for recording, or disarms them all when every track is armed already.",
         .aliases = {"record all tracks", "disarm all tracks", "record enable all"},
         .category = "Tracks",
         .shortcut = "Shift+Alt+R",
         .schema = {{"properties", {{"value", {{"type", "boolean"}}}}}},
         .run =
             [&controller](const Json& params) {
                 const auto& tracks = controller.project().document().tracks;
                 const bool value =
                     optionalParam<bool>(params, "value").value_or(!std::ranges::all_of(tracks, &model::Track::armed));
                 const auto ids = tracks | std::views::transform(&model::Track::id) | std::ranges::to<std::vector>();
                 controller.project().group(value ? "arm all tracks" : "disarm all tracks", [&] {
                     for (const model::TrackId& id : ids) {
                         editTrack(controller, id, "arm track", [&](model::Track& track) { track.armed = value; });
                     }
                 });
                 for (const model::TrackId& id : ids) {
                     controller.setLiveArmed(id, value);
                 }
                 return projectState(controller);
             },
         .menus = {kTrackMenu}});
}

void registerTrackLooks(Registry& registry, ProjectController& controller, const Nav& nav) {
    registry.addAction(
        {.id = "track.setColor",
         .title = "Set track color",
         .category = "Tracks",
         .schema = {{"required", {"color"}},
                    {"properties", {{"track", {{"type", "string"}}}, {"color", {{"type", "integer"}}}}}},
         .run =
             [&controller, &nav](const Json& params) {
                 const auto color = param<int>(params, "color");
                 if (color < 0 || color >= kTrackColors) {
                     throw AppError("color must be 0 to " + std::to_string(kTrackColors - 1));
                 }
                 return editTargets(controller, nav, params, "set track color", [&](const model::TrackId& id) {
                     editTrack(controller, id, "set track color", [&](model::Track& track) { track.color = color; });
                 });
             },
         .menus = trackMenus(),
         .submenu = colorChoices,
         .current =
             [&controller, &nav](const Json& params) {
                 return std::to_string(controller.project().track(targetTrack(controller, nav, params))->color);
             }});
    registry.addAction(
        {.id = "track.setIcon",
         .title = "Set track icon",
         .category = "Tracks",
         .schema = {{"required", {"icon"}},
                    {"properties", {{"track", {{"type", "string"}}}, {"icon", {{"type", "string"}}}}}},
         .run =
             [&controller, &nav](const Json& params) {
                 const auto icon = param<std::string>(params, "icon");
                 return editTargets(controller, nav, params, "set track icon", [&](const model::TrackId& id) {
                     editTrack(controller, id, "set track icon", [&](model::Track& track) { track.icon = icon; });
                 });
             },
         .menus = trackMenus(),
         .submenu =
             [] {
                 return kTrackIcons | std::views::transform([](const TrackIcon& icon) {
                            return MenuChoice{.id = std::string{icon.icon},
                                              .title = std::string{icon.title},
                                              .params = {{"icon", icon.icon}},
                                              .icon = std::string{icon.icon}};
                        }) |
                        std::ranges::to<std::vector>();
             },
         .current =
             [&controller, &nav](const Json& params) {
                 return controller.project().track(targetTrack(controller, nav, params))->icon;
             }});
}

void registerTrackSelection(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "track.select",
                        .title = "Select track",
                        .description = "With extend it adds the track to the selection or takes it out, and with "
                                       "range it selects every track from the selected track to this one.",
                        .aliases = {"show track in inspector"},
                        .category = "Tracks",
                        .schema = {{"required", {"track"}},
                                   {"properties",
                                    {{"track", {{"type", "string"}}},
                                     {"extend", {{"type", "boolean"}}},
                                     {"range", {{"type", "boolean"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            if (optionalParam<bool>(params, "range").value_or(false)) {
                                selectRange(controller, nav, id);
                            } else if (optionalParam<bool>(params, "extend").value_or(false)) {
                                toggleSelected(nav, qs(id.value));
                            } else {
                                nav.setSelectedTrack(qs(id.value));
                            }
                            return trackResult(controller, id);
                        }});
    registry.addAction({.id = "track.selectAll",
                        .title = "Select all tracks",
                        .aliases = {"select every track"},
                        .category = "Tracks",
                        .run =
                            [&controller, &nav](const Json&) {
                                nav.setSelectedTracks(controller.project().document().tracks |
                                                      std::views::transform([](const model::Track& track) {
                                                          return qs(track.id.value);
                                                      }) |
                                                      std::ranges::to<QStringList>());
                                return nav.toJson();
                            },
                        .menus = {kTrackMenu}});
    registry.addAction({.id = "track.showMenu",
                        .title = "Show track menu",
                        .aliases = {"track context menu", "track options"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto id = targetTrack(controller, nav, params);
                                if (nav.selectedTracks().contains(qs(id.value))) {
                                    nav.selectTracks(nav.selectedTracks(), qs(id.value));
                                } else {
                                    nav.setSelectedTrack(qs(id.value));
                                }
                                nav.setTrackMenu(qs(id.value));
                                return Json{{"trackMenu", id.value}};
                            },
                        .windowOnly = true});
    registry.addAction({.id = "track.selectPrevious",
                        .title = "Select previous track",
                        .aliases = {"track above"},
                        .category = "Tracks",
                        .shortcut = "Up",
                        .run = [&controller, &nav](const Json&) { return selectNeighbor(controller, nav, -1); }});
    registry.addAction({.id = "track.selectNext",
                        .title = "Select next track",
                        .aliases = {"track below"},
                        .category = "Tracks",
                        .shortcut = "Down",
                        .run = [&controller, &nav](const Json&) { return selectNeighbor(controller, nav, 1); }});
}

}  // namespace

model::TrackId targetTrack(const ProjectController& controller, const Nav& nav, const Json& params) {
    if (params.contains("track")) {
        return trackParam(controller.project(), params);
    }
    const model::TrackId selected{nav.selectedTrack().toStdString()};
    if (controller.project().track(selected) == nullptr) {
        throw AppError("select a track first, or pass track");
    }
    return selected;
}

std::vector<model::TrackId> targetTracks(const ProjectController& controller, const Nav& nav, const Json& params) {
    const model::TrackId target = targetTrack(controller, nav, params);
    const QStringList selected = nav.selectedTracks();
    if (!optionalParam<bool>(params, "selection").value_or(false) || !selected.contains(qs(target.value))) {
        return {target};
    }
    return controller.project().document().tracks | std::views::transform(&model::Track::id) |
           std::views::filter([&](const model::TrackId& id) { return selected.contains(qs(id.value)); }) |
           std::ranges::to<std::vector>();
}

Json editTargets(ProjectController& controller, const Nav& nav, const Json& params, const std::string& name,
                 const std::function<void(const model::TrackId&)>& edit) {
    const model::TrackId target = targetTrack(controller, nav, params);
    const std::vector<model::TrackId> ids = targetTracks(controller, nav, params);
    if (ids.size() == 1) {
        edit(target);
    } else {
        controller.project().group(name, [&] { std::ranges::for_each(ids, edit); });
    }
    return trackResult(controller, target);
}

std::vector<MenuChoice> colorChoices() {
    return std::views::iota(0, kTrackColors) | std::views::transform([](int color) {
               return MenuChoice{.id = std::to_string(color),
                                 .title = std::string{kColorNames[static_cast<std::size_t>(color)]},
                                 .params = {{"color", color}},
                                 .color = color};
           }) |
           std::ranges::to<std::vector>();
}

void registerTrackEditActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "track.beginRename",
                        .title = "Rename track",
                        .aliases = {"edit track name"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto id = targetTrack(controller, nav, params);
                                nav.setRenamingTrack(qs(id.value));
                                return trackResult(controller, id);
                            },
                        .menus = trackMenus(),
                        .windowOnly = true});
    registry.addAction({.id = "track.rename",
                        .title = "Set track name",
                        .category = "Tracks",
                        .schema = {{"required", {"name"}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto id = targetTrack(controller, nav, params);
                            const auto name = param<std::string>(params, "name");
                            nav.setRenamingTrack({});
                            return editTrack(controller, id, "rename track to " + name,
                                             [&](model::Track& track) { track.name = name; });
                        }});
    registry.addAction({.id = "track.duplicate",
                        .title = "Duplicate track",
                        .aliases = {"copy track"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto id = targetTrack(controller, nav, params);
                                model::Track copy = duplicateTrack(*controller.project().track(id));
                                const model::TrackId copyId = copy.id;
                                controller.project().addTrack(std::move(copy), trackIndex(controller, id) + 1);
                                nav.setSelectedTrack(qs(copyId.value));
                                return trackResult(controller, copyId);
                            },
                        .menus = trackMenus()});
    registry.addAction({.id = "track.remove",
                        .title = "Delete track",
                        .aliases = {"remove track"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                editTargets(controller, nav, params, "remove tracks",
                                            [&](const model::TrackId& id) { removeTrack(controller, id); });
                                return projectState(controller);
                            },
                        .menus = trackMenus()});
    registry.addAction({.id = "track.move",
                        .title = "Move track",
                        .aliases = {"reorder track"},
                        .category = "Tracks",
                        .schema = {{"required", {"track", "index"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            if (!controller.project().moveTrack(id, param<std::size_t>(params, "index"))) {
                                throw AppError("index must be below the track count");
                            }
                            return projectState(controller);
                        }});
    registerTrackSelection(registry, controller, nav);
    registerTrackFlags(registry, controller, nav);
    registry.addAction(
        {.id = "track.setGain",
         .title = "Set track gain",
         .description = "With selection every selected track moves by the same number of dB.",
         .aliases = {"track volume", "fader"},
         .category = "Tracks",
         .schema =
             {{"required", {"track", "db"}},
              {"properties",
               {{"track", {{"type", "string"}}}, {"db", {{"type", "number"}}}, {"selection", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) {
             const auto target = trackParam(controller.project(), params);
             const auto wanted = param<double>(params, "db");
             if (wanted < kAutomationGainMinDb || wanted > kAutomationGainMaxDb) {
                 throw AppError(std::format("db must be from {} to {}", kAutomationGainMinDb, kAutomationGainMaxDb));
             }
             const double change = wanted - controller.project().track(target)->gainDb;
             return editTargets(controller, nav, params, "set track gain", [&](const model::TrackId& id) {
                 const double db = id == target ? wanted
                                                : std::clamp(controller.project().track(id)->gainDb + change,
                                                             kAutomationGainMinDb, kAutomationGainMaxDb);
                 if (!controller.writeAutomation(id, "gain", db)) {
                     editTrack(controller, id, "set track gain", [&](model::Track& track) { track.gainDb = db; });
                 }
                 controller.setLiveGain(id, db);
             });
         }});
    registry.addAction(
        {.id = "track.setTrim",
         .title = "Set track trim",
         .description = "Trims the track's input level by db, from -24 to 24, before its inserts and fader. With "
                        "selection every selected track gets the trim.",
         .aliases = {"input trim", "input gain", "preamp gain"},
         .category = "Tracks",
         .schema = {{"required", {"track", "db"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"db", {{"type", "number"}, {"minimum", -kMaxTrimDb}, {"maximum", kMaxTrimDb}}},
                      {"selection", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) {
             trackParam(controller.project(), params);
             const auto db = param<double>(params, "db");
             if (!(std::abs(db) <= kMaxTrimDb)) {
                 throw AppError(std::format("db must be from {} to {}", -kMaxTrimDb, kMaxTrimDb));
             }
             return editTargets(controller, nav, params, "set track trim", [&](const model::TrackId& id) {
                 editTrack(controller, id, "set track trim", [&](model::Track& track) { track.trimDb = db; });
             });
         }});
    registry.addAction(
        {.id = "track.setPan",
         .title = "Set track pan",
         .description = "With selection every selected track pans by the same amount.",
         .aliases = {"balance", "pan knob"},
         .category = "Tracks",
         .schema = {{"required", {"track", "value"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"value", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}},
                      {"selection", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) {
             const auto target = trackParam(controller.project(), params);
             const auto wanted = param<double>(params, "value");
             if (wanted < -1.0 || wanted > 1.0) {
                 throw AppError("value must be from -1 (hard left) to 1 (hard right)");
             }
             const double change = wanted - controller.project().track(target)->pan;
             return editTargets(controller, nav, params, "set track pan", [&](const model::TrackId& id) {
                 const double value =
                     id == target ? wanted : std::clamp(controller.project().track(id)->pan + change, -1.0, 1.0);
                 if (!controller.writeAutomation(id, "pan", value)) {
                     editTrack(controller, id, "set track pan", [&](model::Track& track) { track.pan = value; });
                 }
                 controller.setLivePan(id, value);
             });
         }});
    registry.addAction(
        {.id = "track.setRecordDelay",
         .title = "Set track record delay",
         .description =
             "Moves the track's next takes later by ms milliseconds, or earlier when negative, to make up for "
             "an instrument whose audio or MIDI arrives late. With selection every selected track gets the delay.",
         .aliases = {"latency compensation", "record offset", "track delay"},
         .category = "Tracks / Routing",
         .schema = {{"required", {"ms"}},
                    {"properties",
                     {{"track", {{"type", "string"}}},
                      {"ms", {{"type", "number"}, {"minimum", -kMaxRecordDelayMs}, {"maximum", kMaxRecordDelayMs}}},
                      {"selection", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) {
             const auto ms = param<double>(params, "ms");
             if (!(std::abs(ms) <= kMaxRecordDelayMs)) {
                 throw AppError(std::format("ms must be from {} to {}", -kMaxRecordDelayMs, kMaxRecordDelayMs));
             }
             return editTargets(controller, nav, params, "set track record delay", [&](const model::TrackId& id) {
                 editTrack(controller, id, "set track record delay",
                           [&](model::Track& track) { track.recordDelayMs = ms; });
             });
         }});
    registerTrackLooks(registry, controller, nav);
}

}  // namespace emerick::app
