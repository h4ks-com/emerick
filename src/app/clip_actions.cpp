#include <QStringList>
#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/audio_file.hpp"
#include "emerick/app/midi_import.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/clip_edit.hpp"

namespace emerick::app {

Json clipResult(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    return found ? clipState(*found->clip, controller.project().document().tempo) : Json::object();
}

Json clipResults(const ProjectController& controller, const std::vector<model::ClipId>& ids) {
    return Json{{"clips", ids | std::views::transform([&](const model::ClipId& id) {
                              return clipResult(controller, id);
                          }) | std::ranges::to<Json::array_t>()}};
}

std::vector<model::ClipId> targetClips(const ProjectController& controller, const Nav& nav, const Json& params) {
    if (params.contains("clip")) {
        return {clipParam(controller.project(), params)};
    }
    auto selected = selectedClips(controller, nav);
    if (selected.empty()) {
        throw AppError("select a clip first, or pass clip");
    }
    return selected;
}

namespace {

template <class T>
T edited(std::expected<T, std::string> result) {
    if (!result) {
        throw AppError(result.error());
    }
    return std::move(*result);
}

model::ClipRef clipRef(const ProjectController& controller, const model::ClipId& id) {
    const auto found = controller.project().findClip(id);
    if (!found) {
        throw AppError("no clip " + id.value);
    }
    return *found;
}

const model::Clip& clipOf(const ProjectController& controller, const model::ClipId& id) {
    return *clipRef(controller, id).clip;
}

// Replaces the clip with id by `replacement`, then adds `added` to the same track, as one undo step.
void replaceClip(ProjectController& controller, const model::ClipId& id, std::string name, model::Clip replacement,
                 std::vector<model::Clip> added = {}) {
    controller.project().editTrack(clipRef(controller, id).track->id, std::move(name), [&](model::Track& edit) {
        *std::ranges::find(edit.clips, id, [](const model::Clip& c) { return model::clipId(c); }) =
            std::move(replacement);
        std::ranges::move(added, std::back_inserter(edit.clips));
    });
}

model::AudioClip audioOnly(const model::Clip& clip) {
    return std::visit(
        core::Overloaded{[](const model::AudioClip& audio) { return audio; },
                         [](const model::MidiClip& midi) -> model::AudioClip {
                             throw AppError("clip " + midi.id.value + " is a MIDI clip, which has no gain");
                         }},
        clip);
}

// Selecting clips also selects the first clip's track, so the inspector shows both.
void select(const ProjectController& controller, Nav& nav, const std::vector<model::ClipId>& ids) {
    nav.setSelectedClips(ids | std::views::transform([](const model::ClipId& id) { return qs(id.value); }) |
                         std::ranges::to<QStringList>());
    if (!ids.empty()) {
        nav.setSelectedTrack(qs(clipRef(controller, ids.front()).track->id.value));
    }
}

model::TrackId addedTrack(const Registry& registry, const std::string& action, const std::string& name) {
    return model::TrackId{registry.run(action, {{"name", name}}).at("id").get<std::string>()};
}

model::TrackId trackOfType(const Registry& registry, const ProjectController& controller, const model::TrackId& id,
                           ClipType type, const std::string& label) {
    const model::Track& track = *controller.project().track(id);
    if (clipTypeOf(registry.trackType(track.kind)) != type) {
        throw AppError("track " + track.name + " is not " + label + " track");
    }
    return id;
}

Json importClip(const Registry& registry, ProjectController& controller, const Nav& nav, const Json& params) {
    const bool newTrack = optionalParam<bool>(params, "newTrack").value_or(false);
    const std::optional<model::TrackId> target =
        newTrack ? std::nullopt
                 : std::optional{trackOfType(registry, controller, targetTrack(controller, nav, params),
                                             ClipType::audio, "an audio")};
    const std::filesystem::path source = registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read);
    const core::TempoMap& tempo = controller.project().document().tempo;
    model::AudioClip clip = clipFromFile(source, controller.folder(), optionalPosition(params, "at", tempo).value_or(0),
                                         tempo, controller.sampleRate());
    const model::ClipId clipId = clip.id;
    const std::string name = "import " + source.filename().string();
    controller.project().group(name, [&] {
        const model::TrackId id = target ? *target : addedTrack(registry, "track.addAudio", source.stem().string());
        controller.project().editTrack(id, name,
                                       [&](model::Track& edited) { edited.clips.emplace_back(std::move(clip)); });
    });
    return clipResult(controller, clipId);
}

// A file with one part goes onto the given MIDI track; otherwise each part gets a new MIDI track named after it.
Json importMidi(const Registry& registry, ProjectController& controller, const Json& params) {
    const std::optional<model::TrackId> target =
        params.contains("track")
            ? std::optional{trackOfType(registry, controller, trackParam(controller.project(), params), ClipType::midi,
                                        "a MIDI")}
            : std::nullopt;
    const std::filesystem::path source = registry.checkedPath(param<std::string>(params, "path"), PathAccess::Read);
    const core::TempoMap& tempo = controller.project().document().tempo;
    const core::Ticks at = optionalPosition(params, "at", tempo).value_or(0);
    std::vector<MidiPart> parts = readMidiParts(source, source.stem().string(), tempo.ticksPerBar());
    if (parts.empty()) {
        throw AppError(source.filename().string() + " has no notes");
    }
    std::vector<model::ClipId> ids;
    const std::string name = "import " + source.filename().string();
    controller.project().group(name, [&] {
        for (MidiPart& part : parts) {
            part.clip.start = at;
            ids.push_back(part.clip.id);
            const model::TrackId id =
                target && parts.size() == 1 ? *target : addedTrack(registry, "track.addMidi", part.name);
            controller.project().editTrack(
                id, name, [&](model::Track& edited) { edited.clips.emplace_back(std::move(part.clip)); });
        }
    });
    return clipResults(controller, ids);
}

Json splitClips(ProjectController& controller, const Nav& nav, const Json& params) {
    const core::TempoMap& tempo = controller.project().document().tempo;
    const core::Ticks at = optionalPosition(params, "at", tempo).value_or(controller.positionTicks());
    const auto splitOne = [&](const model::ClipId& id) {
        auto [left, right] = edited(model::split(clipOf(controller, id), at, tempo, controller.sampleRate()));
        const model::ClipId rightId = model::clipId(right);
        replaceClip(controller, id, "split clip", std::move(left), {std::move(right)});
        return Json{{"left", clipResult(controller, id)}, {"right", clipResult(controller, rightId)}};
    };
    if (params.contains("clip")) {
        return splitOne(clipParam(controller.project(), params));
    }
    const auto inside = [&](const model::ClipId& id) {
        const model::Clip& clip = clipOf(controller, id);
        return at > model::clipStart(clip) && at < model::clipEnd(clip);
    };
    const auto ids = targetClips(controller, nav, params) | std::views::filter(inside) | std::ranges::to<std::vector>();
    if (ids.empty()) {
        throw AppError("the split position is not inside a selected clip");
    }
    return Json{{"splits", ids | std::views::transform(splitOne) | std::ranges::to<Json::array_t>()}};
}

Json duplicateClips(ProjectController& controller, Nav& nav, const Json& params) {
    const auto at = optionalPosition(params, "at", controller.project().document().tempo);
    const auto duplicateOne = [&](const model::ClipId& id) {
        const model::Clip& clip = clipOf(controller, id);
        model::Clip copy = model::duplicateClip(clip, at.value_or(model::clipEnd(clip)));
        model::ClipId copyId = model::clipId(copy);
        replaceClip(controller, id, "duplicate clip", clip, {std::move(copy)});
        return copyId;
    };
    if (params.contains("clip")) {
        return clipResult(controller, duplicateOne(clipParam(controller.project(), params)));
    }
    const auto copies =
        targetClips(controller, nav, params) | std::views::transform(duplicateOne) | std::ranges::to<std::vector>();
    select(controller, nav, copies);
    return clipResults(controller, copies);
}

struct ClipMove {
    model::ClipId id;
    model::TrackId from;
    model::TrackId to;
    core::Ticks start;
};

// Moves the clip to start on track; with selection, the other selected clips keep their offsets in time and rows.
Json moveClips(const Registry& registry, ProjectController& controller, const Nav& nav, const Json& params) {
    const auto id = clipParam(controller.project(), params);
    const model::ClipRef dragged = clipRef(controller, id);
    const auto& tracks = controller.project().document().tracks;
    const auto rowOf = [&](const model::TrackId& track) {
        return std::ranges::find(tracks, track, &model::Track::id) - tracks.begin();
    };
    const core::Ticks shift = optionalPosition(params, "start", controller.project().document().tempo)
                                  .value_or(model::clipStart(*dragged.clip)) -
                              model::clipStart(*dragged.clip);
    const auto rows =
        params.contains("track") ? rowOf(trackParam(controller.project(), params)) - rowOf(dragged.track->id) : 0;
    auto ids = selectedClips(controller, nav);
    if (!optionalParam<bool>(params, "selection").value_or(false) || !std::ranges::contains(ids, id)) {
        ids = {id};
    }
    const auto plan = ids | std::views::transform([&](const model::ClipId& clip) {
                          const model::ClipRef ref = clipRef(controller, clip);
                          const auto row = rowOf(ref.track->id) + rows;
                          if (row < 0 || row >= std::ssize(tracks)) {
                              throw AppError("the move goes past the first or last track");
                          }
                          const model::Track& to = tracks[static_cast<std::size_t>(row)];
                          if (!trackTakesClip(registry, to, *ref.clip)) {
                              throw AppError("track " + to.name + " takes no clips of this kind");
                          }
                          const core::Ticks start = model::clipStart(*ref.clip) + shift;
                          if (start < 0) {
                              throw AppError("start must be at or after bar 1");
                          }
                          return ClipMove{clip, ref.track->id, to.id, start};
                      }) |
                      std::ranges::to<std::vector>();
    controller.project().group("move clip", [&] {
        for (const ClipMove& move : plan) {
            model::Clip moved = model::moveClip(clipOf(controller, move.id), move.start);
            if (move.from == move.to) {
                replaceClip(controller, move.id, "move clip", std::move(moved));
                continue;
            }
            controller.project().editTrack(move.from, "move clip", [&](model::Track& edit) {
                std::erase_if(edit.clips, [&](const model::Clip& c) { return model::clipId(c) == move.id; });
            });
            controller.project().editTrack(move.to, "move clip",
                                           [&](model::Track& edit) { edit.clips.push_back(std::move(moved)); });
        }
    });
    return ids.size() == 1 ? clipResult(controller, id) : clipResults(controller, ids);
}

}  // namespace

void deleteClips(ProjectController& controller, const std::vector<model::ClipId>& ids) {
    controller.project().group("delete clip", [&] {
        for (const model::ClipId& id : ids) {
            const model::TrackId track = clipRef(controller, id).track->id;
            controller.project().editTrack(track, "delete clip", [&](model::Track& edited) {
                std::erase_if(edited.clips, [&](const model::Clip& c) { return model::clipId(c) == id; });
            });
        }
    });
}

std::vector<model::ClipId> selectedClips(const ProjectController& controller, const Nav& nav) {
    return nav.selectedClips() |
           std::views::transform([](const QString& id) { return model::ClipId{id.toStdString()}; }) |
           std::views::filter([&](const model::ClipId& id) { return controller.project().findClip(id).has_value(); }) |
           std::ranges::to<std::vector>();
}

namespace {

void registerSelectionActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addAction({.id = "selection.set",
                        .title = "Select clips",
                        .aliases = {"select clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clips"}}, {"properties", {{"clips", {{"type", "array"}}}}}},
                        .run = [&controller, &nav](const Json& params) {
                            const auto ids = param<std::vector<std::string>>(params, "clips") |
                                             std::views::transform([&](const std::string& id) {
                                                 return clipParam(controller.project(), Json{{"clip", id}});
                                             }) |
                                             std::ranges::to<std::vector>();
                            select(controller, nav, ids);
                            return nav.toJson();
                        }});
    registry.addAction({.id = "selection.clear",
                        .title = "Deselect all clips",
                        .aliases = {"clear selection", "select none"},
                        .category = "Clips",
                        .shortcut = "Ctrl+Shift+A",
                        .run =
                            [&nav](const Json&) {
                                nav.setSelectedClips({});
                                return nav.toJson();
                            },
                        .menus = {kEditMenu}});
    registry.addSetting({.id = "edit.snap",
                         .title = "Snap to beat grid",
                         .aliases = {"snap", "grid"},
                         .category = "Settings / Editing",
                         .type = SettingType::Boolean,
                         .defaultValue = true,
                         .hint = "Hold Command (Ctrl on Linux and Windows) while dragging to snap the other way",
                         .location = "settings.general"});
    registry.addAction(
        {.id = "edit.toggleSnap",
         .title = "Toggle snap to grid",
         .aliases = {"snap on", "snap off", "grid snap"},
         .category = "Clips",
         .shortcut = "N",
         .run =
             [&registry](const Json&) {
                 return Json{
                     {"snap", registry.setSetting("edit.snap", !registry.settingValue("edit.snap").get<bool>())}};
             },
         .menus = {kViewMenu}});
    // Undo, delete and track removal can take selected clips away, so we drop their ids from the selection.
    controller.project().addListener([&controller, &nav](const model::Change&) {
        const auto kept = selectedClips(controller, nav);
        if (std::cmp_not_equal(kept.size(), nav.selectedClips().size())) {
            nav.setSelectedClips(kept | std::views::transform([](const model::ClipId& id) { return qs(id.value); }) |
                                 std::ranges::to<QStringList>());
        }
    });
}

}  // namespace

ClipType clipTypeOf(const TrackType* type) {
    if (type == nullptr || !type->laneComponent.empty()) {
        return ClipType::none;
    }
    if (type->routes.audio) {
        return ClipType::audio;
    }
    return type->routes.midi ? ClipType::midi : ClipType::none;
}

bool trackTakesClip(const Registry& registry, const model::Track& track, const model::Clip& clip) {
    const ClipType type = std::visit(core::Overloaded{[](const model::AudioClip&) { return ClipType::audio; },
                                                      [](const model::MidiClip&) { return ClipType::midi; }},
                                     clip);
    return clipTypeOf(registry.trackType(track.kind)) == type;
}

void registerClipActions(Registry& registry, ProjectController& controller, Nav& nav) {
    const auto tempo = [&controller]() -> const core::TempoMap& { return controller.project().document().tempo; };

    registry.addAction({.id = "clip.move",
                        .title = "Move clip",
                        .aliases = {"move clip to track", "drag clip"},
                        .category = "Clips",
                        .schema = {{"required", {"clip"}},
                                   {"properties",
                                    {{"start", Json::object()},
                                     {"track", {{"type", "string"}}},
                                     {"selection", {{"type", "boolean"}}}}}},
                        .run = [&registry, &controller, &nav](const Json& params) {
                            return moveClips(registry, controller, nav, params);
                        }});
    registry.addAction(
        {.id = "clip.resize",
         .title = "Resize clip",
         .aliases = {"trim clip"},
         .category = "Clips",
         .schema = {{"required", {"clip"}},
                    {"properties",
                     {{"start", Json::object()}, {"length", {{"type", "integer"}}}, {"end", Json::object()}}}},
         .run = [&controller, tempo](const Json& params) {
             const auto id = clipParam(controller.project(), params);
             const auto start = optionalPosition(params, "start", tempo());
             const auto end = optionalPosition(params, "end", tempo());
             auto length = optionalParam<core::Ticks>(params, "length");
             if (!start && !length && !end) {
                 throw AppError("pass start, length, end or a mix");
             }
             model::Clip clip = clipOf(controller, id);
             if (start) {
                 clip = edited(model::trimStart(clip, *start, tempo(), controller.sampleRate()));
             }
             if (end) {
                 length = *end - model::clipStart(clip);
             }
             if (length) {
                 clip = edited(model::setLength(clip, *length));
             }
             replaceClip(controller, id, "resize clip", std::move(clip));
             return clipResult(controller, id);
         }});
    registry.addAction({.id = "clip.split",
                        .title = "Split clip at playhead",
                        .aliases = {"cut clip", "split region"},
                        .category = "Clips",
                        .shortcut = "Ctrl+T",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"at", Json::object()}}}},
                        .run = [&controller, &nav](const Json& params) { return splitClips(controller, nav, params); },
                        .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction({.id = "clip.setGain",
                        .title = "Set clip gain",
                        .aliases = {"clip volume"},
                        .category = "Clips",
                        .schema = {{"required", {"clip", "db"}}},
                        .run = [&controller](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            model::AudioClip audio = audioOnly(clipOf(controller, id));
                            audio.gainDb = param<double>(params, "db");
                            replaceClip(controller, id, "set clip gain", std::move(audio));
                            return clipResult(controller, id);
                        }});
    registry.addAction({.id = "clip.delete",
                        .title = "Delete clip",
                        .aliases = {"remove clip", "delete selected clips"},
                        .category = "Clips",
                        .schema = {{"properties", {{"clip", {{"type", "string"}}}}}},
                        .run =
                            [&controller, &nav](const Json& params) {
                                deleteClips(controller, targetClips(controller, nav, params));
                                return projectState(controller);
                            },
                        .menus = {kClipContextMenu}});
    registerFadeActions(registry, controller, nav);
    registerClipLoopActions(registry, controller, nav);
    registry.addAction(
        {.id = "clip.duplicate",
         .title = "Duplicate clip",
         .aliases = {"copy clip", "repeat clip"},
         .category = "Clips",
         .shortcut = "Ctrl+D",
         .schema = {{"properties", {{"clip", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run = [&controller, &nav](const Json& params) { return duplicateClips(controller, nav, params); },
         .menus = {kEditMenu, kClipContextMenu}});
    registry.addAction(
        {.id = "clip.import",
         .title = "Import audio file",
         .aliases = {"add audio file", "import clip"},
         .category = "Clips",
         .schema = {{"required", {"path"}},
                    {"properties",
                     {{"track", {{"type", "string"}}}, {"at", Json::object()}, {"newTrack", {{"type", "boolean"}}}}}},
         .run = [&registry, &controller,
                 &nav](const Json& params) { return importClip(registry, controller, nav, params); },
         .menus = {kTrackMenu},
         .dialog = FileDialog{.param = "path",
                              .mode = FileDialogMode::Open,
                              .nameFilter = "Audio files (*.wav *.aif *.aiff *.flac *.ogg *.oga *.mp3)"}});
    registry.addAction(
        {.id = "midi.import",
         .title = "Import MIDI file",
         .aliases = {"open midi file", "add midi file"},
         .category = "Clips",
         .schema = {{"required", {"path"}}, {"properties", {{"track", {{"type", "string"}}}, {"at", Json::object()}}}},
         .run = [&registry, &controller](const Json& params) { return importMidi(registry, controller, params); },
         .menus = {kTrackMenu},
         .dialog =
             FileDialog{.param = "path", .mode = FileDialogMode::Open, .nameFilter = "MIDI files (*.mid *.midi)"}});
    registry.addAction({.id = "project.clipEvents",
                        .title = "Show clip notes and MIDI events",
                        .category = "Clips",
                        .schema = {{"required", {"clip"}}, {"properties", {{"includeEvents", {{"type", "boolean"}}}}}},
                        .run = [&controller, tempo](const Json& params) {
                            const auto id = clipParam(controller.project(), params);
                            return clipState(clipOf(controller, id), tempo(),
                                             optionalParam<bool>(params, "includeEvents").value_or(true));
                        }});
    registerSelectionActions(registry, controller, nav);
    registerClipEditActions(registry, controller, nav);
    registerNoteActions(registry, controller, nav);
}

}  // namespace emerick::app
