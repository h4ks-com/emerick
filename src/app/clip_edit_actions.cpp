#include <QStringList>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

namespace {

using Clipboard = std::vector<model::Clip>;

void selectClips(Nav& nav, const model::TrackId& track, const std::vector<model::ClipId>& ids) {
    nav.setSelectedTrack(qs(track.value));
    nav.setSelectedClips(ids | std::views::transform([](const model::ClipId& id) { return qs(id.value); }) |
                         std::ranges::to<QStringList>());
}

Json selectionState(const ProjectController& controller, const Nav& nav) {
    const core::TempoMap& tempo = controller.project().document().tempo;
    return Json{{"clips", selectedClips(controller, nav) | std::views::transform([&](const model::ClipId& id) {
                              return clipState(*controller.project().findClip(id)->clip, tempo);
                          }) | std::ranges::to<Json::array_t>()}};
}

std::vector<model::ClipId> requireSelection(const ProjectController& controller, const Nav& nav) {
    auto ids = selectedClips(controller, nav);
    if (ids.empty()) {
        throw AppError("select a clip first");
    }
    return ids;
}

void copyClips(const ProjectController& controller, const Nav& nav, Clipboard& clipboard) {
    clipboard =
        requireSelection(controller, nav) |
        std::views::transform([&](const model::ClipId& id) { return *controller.project().findClip(id)->clip; }) |
        std::ranges::to<std::vector>();
}

// The copies keep their spacing, with the earliest at the playhead or at params' at.
Json pasteClips(const Registry& registry, ProjectController& controller, Nav& nav, const Clipboard& clipboard,
                const Json& params) {
    if (clipboard.empty()) {
        throw AppError("copy a clip first");
    }
    const model::TrackId trackId = targetTrack(controller, nav, params);
    const model::Track& track = *controller.project().track(trackId);
    if (!std::ranges::all_of(clipboard,
                             [&](const model::Clip& clip) { return trackTakesClip(registry, track, clip); })) {
        throw AppError("track " + track.name + " takes no clips of this kind");
    }
    const core::Ticks at =
        optionalPosition(params, "at", controller.project().document().tempo).value_or(controller.positionTicks());
    const core::Ticks earliest = std::ranges::min(clipboard | std::views::transform(model::clipStart));
    const auto copies = clipboard | std::views::transform([&](const model::Clip& clip) {
                            return model::duplicateClip(clip, at + model::clipStart(clip) - earliest);
                        }) |
                        std::ranges::to<std::vector>();
    controller.project().editTrack(
        trackId, "paste clips", [&](model::Track& edit) { std::ranges::copy(copies, std::back_inserter(edit.clips)); });
    selectClips(nav, trackId, copies | std::views::transform(model::clipId) | std::ranges::to<std::vector>());
    return selectionState(controller, nav);
}

// Without a selected clip on the track, step 1 picks the first clip from the playhead and -1 the last before it.
Json selectNeighborClip(ProjectController& controller, Nav& nav, const Json& params, std::ptrdiff_t step) {
    const model::TrackId trackId = targetTrack(controller, nav, params);
    auto clips = controller.project().track(trackId)->clips | std::views::transform([](const model::Clip& clip) {
                     return std::pair{model::clipStart(clip), model::clipId(clip)};
                 }) |
                 std::ranges::to<std::vector>();
    if (clips.empty()) {
        throw AppError("the track has no clips");
    }
    std::ranges::sort(clips);
    const auto selected = selectedClips(controller, nav);
    const auto current =
        std::ranges::find_if(clips, [&](const auto& entry) { return std::ranges::contains(selected, entry.second); });
    std::ptrdiff_t at = 0;
    if (current != clips.end()) {
        at = std::clamp<std::ptrdiff_t>(current - clips.begin() + step, 0, std::ssize(clips) - 1);
    } else {
        const auto after =
            std::ranges::find_if(clips, [&](const auto& entry) { return entry.first >= controller.positionTicks(); });
        at = std::clamp<std::ptrdiff_t>((after - clips.begin()) - (step < 0 ? 1 : 0), 0, std::ssize(clips) - 1);
    }
    selectClips(nav, trackId, {clips[static_cast<std::size_t>(at)].second});
    return selectionState(controller, nav);
}

// Without value, mutes the clips unless every one of them is muted already, as Logic's Mute Region does.
Json setClipsMuted(ProjectController& controller, const Nav& nav, const Json& params) {
    const auto ids =
        params.contains("clips")
            ? param<std::vector<std::string>>(params, "clips") | std::views::transform([&](const std::string& id) {
                  return clipParam(controller.project(), Json{{"clip", id}});
              }) | std::ranges::to<std::vector>()
            : targetClips(controller, nav, params);
    const auto clipOf = [&](const model::ClipId& id) { return controller.project().findClip(id)->clip; };
    const bool value =
        optionalParam<bool>(params, "value").value_or(!std::ranges::all_of(ids, [&](const model::ClipId& id) {
            return model::clipMuted(*clipOf(id));
        }));
    const std::string name = value ? "mute clip" : "unmute clip";
    controller.project().group(name, [&] {
        for (const model::ClipId& id : ids) {
            controller.project().editTrack(
                controller.project().findClip(id)->track->id, name, [&](model::Track& track) {
                    model::setClipMuted(*std::ranges::find(track.clips, id, model::clipId), value);
                });
        }
    });
    return clipResults(controller, ids);
}

Json renameClips(ProjectController& controller, Nav& nav, const Json& params) {
    const auto ids = targetClips(controller, nav, params);
    const auto name = param<std::string>(params, "name");
    nav.setRenamingClip({});
    const std::string undoName = name.empty() ? "clear clip name" : "rename clip to " + name;
    controller.project().group(undoName, [&] {
        for (const model::ClipId& id : ids) {
            controller.project().editTrack(
                controller.project().findClip(id)->track->id, undoName, [&](model::Track& track) {
                    model::setClipName(*std::ranges::find(track.clips, id, model::clipId), name);
                });
        }
    });
    return clipResults(controller, ids);
}

constexpr double kFitMargin = 0.05;

// Fits the selected clips, or the whole project, into the lanes with a small margin on each side.
Json zoomToFit(const ProjectController& controller, Nav& nav) {
    const auto ids = selectedClips(controller, nav);
    const auto clips =
        ids | std::views::transform([&](const model::ClipId& id) { return *controller.project().findClip(id)->clip; }) |
        std::ranges::to<std::vector>();
    const core::Ticks from = clips.empty() ? 0 : std::ranges::min(clips | std::views::transform(model::clipStart));
    const core::Ticks to =
        clips.empty() ? controller.endTicks() : std::ranges::max(clips | std::views::transform(model::clipEnd));
    const auto span = static_cast<double>(std::max(to - from, controller.project().document().tempo.ticksPerBar()));
    if (nav.laneWidth() > 0) {
        nav.setPixelsPerTick(nav.laneWidth() / (span * (1 + 2 * kFitMargin)));
    }
    nav.setScrollTicks(static_cast<double>(from) - span * kFitMargin);
    return Json{{"pixelsPerTick", nav.pixelsPerTick()}, {"scrollTicks", nav.scrollTicks()}};
}

void registerClipboardActions(Registry& registry, ProjectController& controller, Nav& nav) {
    // The copy, cut and paste handlers share the clipboard, and the registry owns the handlers.
    auto clipboard = std::make_shared<Clipboard>();
    registry.addAction({.id = "clip.copy",
                        .title = "Copy clips",
                        .aliases = {"copy"},
                        .category = "Clips",
                        .shortcut = "Ctrl+C",
                        .run =
                            [&controller, &nav, clipboard](const Json&) {
                                copyClips(controller, nav, *clipboard);
                                return Json{{"copied", clipboard->size()}};
                            },
                        .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction({.id = "clip.cut",
                        .title = "Cut clips",
                        .aliases = {"cut"},
                        .category = "Clips",
                        .shortcut = "Ctrl+X",
                        .run =
                            [&controller, &nav, clipboard](const Json&) {
                                copyClips(controller, nav, *clipboard);
                                deleteClips(controller, selectedClips(controller, nav));
                                return Json{{"copied", clipboard->size()}};
                            },
                        .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction(
        {.id = "clip.paste",
         .title = "Paste clips",
         .aliases = {"paste"},
         .category = "Clips",
         .shortcut = "Ctrl+V",
         .schema = {{"properties", {{"track", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run = [&registry, &controller, &nav,
                 clipboard](const Json& params) { return pasteClips(registry, controller, nav, *clipboard, params); },
         .menus = {kEditMenu, kClipContextMenu, kLaneContextMenu}});
}

}  // namespace

void registerClipEditActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registerClipboardActions(registry, controller, nav);
    registry.addAction({.id = "selection.all",
                        .title = "Select all clips",
                        .aliases = {"select everything"},
                        .category = "Clips",
                        .shortcut = "Ctrl+A",
                        .run =
                            [&controller, &nav](const Json&) {
                                nav.setSelectedClips(controller.project().document().tracks |
                                                     std::views::transform(&model::Track::clips) | std::views::join |
                                                     std::views::transform([](const model::Clip& clip) {
                                                         return qs(model::clipId(clip).value);
                                                     }) |
                                                     std::ranges::to<QStringList>());
                                return selectionState(controller, nav);
                            },
                        .menus = {kEditMenu}});
    registry.addAction(
        {.id = "clip.setMute",
         .title = "Mute clip",
         .description = "Mutes the clips, or unmutes them when every one is muted already. A muted "
                        "clip plays nothing and exports nothing.",
         .aliases = {"mute region", "silence clip", "unmute clip"},
         .category = "Clips",
         .shortcut = "Meta+M",
         .schema =
             {{"properties",
               {{"clip", {{"type", "string"}}}, {"clips", {{"type", "array"}}}, {"value", {{"type", "boolean"}}}}}},
         .run = [&controller, &nav](const Json& params) { return setClipsMuted(controller, nav, params); },
         .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction({.id = "clip.beginRename",
                        .title = "Rename clip",
                        .aliases = {"name clip", "rename region"},
                        .category = "Clips",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                const model::ClipId id = targetClips(controller, nav, params).front();
                                nav.setSelectedClips({qs(id.value)});
                                nav.setRenamingClip(qs(id.value));
                                return clipResult(controller, id);
                            },
                        .menus = {kEditMenu, kClipContextMenu},
                        .windowOnly = true});
    registry.addAction(
        {.id = "clip.rename",
         .title = "Set clip name",
         .description = "Names the clips, or the selected clips. The clip header shows the name, and an empty name "
                        "shows the track name and take number again.",
         .category = "Clips",
         .schema = {{"required", {"name"}},
                    {"properties", {{"clip", {{"type", "string"}}}, {"name", {{"type", "string"}}}}}},
         .run = [&controller, &nav](const Json& params) { return renameClips(controller, nav, params); }});
    registry.addAction(
        {.id = "clip.selectPrevious",
         .title = "Select previous clip",
         .aliases = {"clip to the left"},
         .category = "Clips",
         .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
         .run = [&controller, &nav](const Json& params) { return selectNeighborClip(controller, nav, params, -1); },
         .menus = {kEditMenu}});
    registry.addAction(
        {.id = "clip.selectNext",
         .title = "Select next clip",
         .aliases = {"clip to the right"},
         .category = "Clips",
         .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
         .run = [&controller, &nav](const Json& params) { return selectNeighborClip(controller, nav, params, 1); },
         .menus = {kEditMenu}});
    registry.addAction({.id = "view.zoomToFit",
                        .title = "Zoom to fit",
                        .aliases = {"fit selection", "show whole project"},
                        .category = "View",
                        .shortcut = "Z",
                        .run = [&controller, &nav](const Json&) { return zoomToFit(controller, nav); },
                        .menus = {kViewMenu}});
    registry.addAction({.id = "edit.delete",
                        .title = "Delete",
                        .aliases = {"delete selection", "remove selected"},
                        .category = "Clips",
                        .shortcut = "Backspace",
                        .extraShortcuts = {"Delete"},
                        .run =
                            [&registry, &controller, &nav](const Json&) {
                                if (!nav.selectedPoints().isEmpty()) {
                                    registry.run("automation.deletePoints");
                                } else if (const auto clips = selectedClips(controller, nav); !clips.empty()) {
                                    deleteClips(controller, clips);
                                } else if (const auto marker = selectedMarker(controller, nav)) {
                                    deleteMarker(controller, *marker);
                                } else {
                                    editTargets(controller, nav, {{"selection", true}}, "remove tracks",
                                                [&](const model::TrackId& id) { removeTrack(controller, id); });
                                }
                                return projectState(controller);
                            },
                        .menus = {kEditMenu}});
}

}  // namespace emerick::app
