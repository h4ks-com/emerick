#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/action_params.hpp"
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

void registerTrackFlags(Registry& registry, ProjectController& controller, const Nav& nav) {
    for (const TrackFlag& flag : kTrackFlags) {
        registry.addAction(
            {.id = std::string{flag.id},
             .title = std::string{flag.title},
             .aliases = {std::string{flag.alias}},
             .category = "Tracks",
             .shortcut = std::string{flag.shortcut},
             .schema = {{"properties", {{"track", {{"type", "string"}}}, {"value", {{"type", "boolean"}}}}}},
             .run =
                 [&controller, &nav, flag](const Json& params) {
                     const auto id = targetTrack(controller, nav, params);
                     const bool value =
                         optionalParam<bool>(params, "value").value_or(!(controller.project().track(id)->*flag.field));
                     Json result = editTrack(controller, id, std::string{flag.title},
                                             [&](model::Track& track) { track.*flag.field = value; });
                     if (flag.live != nullptr) {
                         (controller.*flag.live)(id, value);
                     }
                     return result;
                 },
             .menus = trackMenus()});
    }
}

void registerTrackLooks(Registry& registry, ProjectController& controller, const Nav& nav) {
    registry.addAction({.id = "track.setColor",
                        .title = "Set track color",
                        .category = "Tracks",
                        .schema = {{"required", {"color"}},
                                   {"properties", {{"track", {{"type", "string"}}}, {"color", {{"type", "integer"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto id = targetTrack(controller, nav, params);
                                const auto color = param<int>(params, "color");
                                if (color < 0 || color >= kTrackColors) {
                                    throw AppError("color must be 0 to " + std::to_string(kTrackColors - 1));
                                }
                                return editTrack(controller, id, "set track color",
                                                 [&](model::Track& track) { track.color = color; });
                            },
                        .menus = trackMenus(),
                        .submenu = colorChoices});
    registry.addAction({.id = "track.setIcon",
                        .title = "Set track icon",
                        .category = "Tracks",
                        .schema = {{"required", {"icon"}},
                                   {"properties", {{"track", {{"type", "string"}}}, {"icon", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const auto id = targetTrack(controller, nav, params);
                                const auto icon = param<std::string>(params, "icon");
                                return editTrack(controller, id, "set track icon",
                                                 [&](model::Track& track) { track.icon = icon; });
                            },
                        .menus = trackMenus(),
                        .submenu =
                            [] {
                                return kTrackIcons | std::views::transform([](const TrackIcon& icon) {
                                           return MenuChoice{.id = std::string{icon.icon},
                                                             .title = std::string{icon.title},
                                                             .params = {{"icon", icon.icon}}};
                                       }) |
                                       std::ranges::to<std::vector>();
                            }});
}

void registerTrackSelection(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "track.select",
                        .title = "Select track",
                        .aliases = {"show track in inspector"},
                        .category = "Tracks",
                        .schema = {{"required", {"track"}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            nav.setSelectedTrack(qs(id.value));
                            return trackResult(controller, id);
                        }});
    registry.addAction({.id = "track.showMenu",
                        .title = "Show track menu",
                        .aliases = {"track context menu", "track options"},
                        .category = "Tracks",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto id = targetTrack(controller, nav, params);
                            nav.setSelectedTrack(qs(id.value));
                            nav.setTrackMenu(qs(id.value));
                            return Json{{"trackMenu", id.value}};
                        }});
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

std::vector<MenuChoice> colorChoices() {
    return std::views::iota(0, kTrackColors) | std::views::transform([](int color) {
               return MenuChoice{.id = std::to_string(color),
                                 .title = std::string{kColorNames[static_cast<std::size_t>(color)]},
                                 .params = {{"color", color}}};
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
                        .menus = trackMenus()});
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
                                removeTrack(controller, targetTrack(controller, nav, params));
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
    registry.addAction({.id = "track.setGain",
                        .title = "Set track gain",
                        .aliases = {"track volume", "fader"},
                        .category = "Tracks",
                        .schema = {{"required", {"track", "db"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto db = param<double>(params, "db");
                            const bool written = controller.writeAutomation(id, "gain", db);
                            Json result = written ? trackResult(controller, id)
                                                  : editTrack(controller, id, "set track gain",
                                                              [&](model::Track& track) { track.gainDb = db; });
                            controller.setLiveGain(id, db);
                            return result;
                        }});
    registry.addAction({.id = "track.setPan",
                        .title = "Set track pan",
                        .aliases = {"balance", "pan knob"},
                        .category = "Tracks",
                        .schema = {{"required", {"track", "value"}},
                                   {"properties", {{"value", {{"type", "number"}, {"minimum", -1}, {"maximum", 1}}}}}},
                        .run = [&controller](const Json& params) {
                            const auto id = trackParam(controller.project(), params);
                            const auto value = param<double>(params, "value");
                            if (value < -1.0 || value > 1.0) {
                                throw AppError("value must be from -1 (hard left) to 1 (hard right)");
                            }
                            const bool written = controller.writeAutomation(id, "pan", value);
                            Json result = written ? trackResult(controller, id)
                                                  : editTrack(controller, id, "set track pan",
                                                              [&](model::Track& track) { track.pan = value; });
                            controller.setLivePan(id, value);
                            return result;
                        }});
    registerTrackLooks(registry, controller, nav);
}

}  // namespace emerick::app
