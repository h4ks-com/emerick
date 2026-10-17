#include "emerick/app/project_actions.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"

namespace emerick::app {

namespace {

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 400.0;
constexpr std::array kDenominators{1, 2, 4, 8, 16};

void registerProjectFileActions(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "project.new",
                        .title = "New project",
                        .aliases = {"start over", "empty project"},
                        .category = "Project",
                        .run = [&controller](const Json&) {
                            controller.newProject();
                            return projectState(controller);
                        }});
    registry.addAction({.id = "project.open",
                        .title = "Open project",
                        .aliases = {"load project"},
                        .category = "Project",
                        .schema = {{"required", {"path"}}},
                        .run = [&controller](const Json& params) {
                            controller.open(param<std::string>(params, "path"));
                            return projectState(controller);
                        }});
    registry.addAction({.id = "project.save",
                        .title = "Save project",
                        .category = "Project",
                        .shortcut = "Ctrl+S",
                        .run = [&controller](const Json&) {
                            controller.save();
                            return projectState(controller);
                        }});
    registry.addAction({.id = "project.saveAs",
                        .title = "Save project as",
                        .aliases = {"save to folder"},
                        .category = "Project",
                        .schema = {{"required", {"path"}}},
                        .run = [&controller](const Json& params) {
                            controller.saveAs(param<std::string>(params, "path"));
                            return projectState(controller);
                        }});
    registry.addAction({.id = "project.undo",
                        .title = "Undo",
                        .category = "Project",
                        .shortcut = "Ctrl+Z",
                        .enabled = [&controller] { return controller.project().undoName().has_value(); },
                        .run =
                            [&controller](const Json&) {
                                controller.project().undo();
                                return projectState(controller);
                            }});
    registry.addAction({.id = "project.redo",
                        .title = "Redo",
                        .category = "Project",
                        .shortcut = "Ctrl+Shift+Z",
                        .enabled = [&controller] { return controller.project().redoName().has_value(); },
                        .run =
                            [&controller](const Json&) {
                                controller.project().redo();
                                return projectState(controller);
                            }});
    registry.addAction({.id = "project.setTempo",
                        .title = "Set tempo and meter",
                        .aliases = {"bpm", "time signature"},
                        .category = "Project",
                        .schema = {{"properties",
                                    {{"bpm", {{"type", "number"}}},
                                     {"numerator", {{"type", "integer"}}},
                                     {"denominator", {{"type", "integer"}}}}}},
                        .run = [&controller](const Json& params) {
                            core::TempoMap tempo = controller.project().document().tempo;
                            tempo.bpm = optionalParam<double>(params, "bpm").value_or(tempo.bpm);
                            tempo.meter.numerator =
                                optionalParam<int>(params, "numerator").value_or(tempo.meter.numerator);
                            tempo.meter.denominator =
                                optionalParam<int>(params, "denominator").value_or(tempo.meter.denominator);
                            if (tempo.bpm < kMinBpm || tempo.bpm > kMaxBpm) {
                                throw AppError("bpm must be 20 to 400");
                            }
                            if (tempo.meter.numerator < 1 ||
                                std::ranges::find(kDenominators, tempo.meter.denominator) == kDenominators.end()) {
                                throw AppError(
                                    "the meter needs a numerator of 1 or more and a denominator of 1, 2, 4, "
                                    "8 or 16");
                            }
                            controller.project().setTempo(tempo);
                            return tempoState(tempo);
                        }});
}

void registerTransportActions(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "transport.play",
                        .title = "Play",
                        .aliases = {"start playback"},
                        .category = "Transport",
                        .run = [&controller](const Json&) {
                            controller.play();
                            return transportState(controller);
                        }});
    registry.addAction({.id = "transport.togglePlay",
                        .title = "Play or pause",
                        .aliases = {"pause", "resume"},
                        .category = "Transport",
                        .shortcut = "Space",
                        .run = [&controller](const Json&) {
                            if (controller.playing()) {
                                controller.stop();
                            } else {
                                controller.play();
                            }
                            return transportState(controller);
                        }});
    // We return to the start on a stop while stopped, as Logic's stop button does.
    registry.addAction({.id = "transport.stop",
                        .title = "Stop",
                        .aliases = {"stop recording"},
                        .category = "Transport",
                        .run = [&controller](const Json&) {
                            if (controller.playing()) {
                                controller.stop();
                            } else {
                                controller.locate(0);
                            }
                            return transportState(controller);
                        }});
    registry.addAction({.id = "transport.gotoStart",
                        .title = "Go to beginning",
                        .aliases = {"rewind", "return to zero"},
                        .category = "Transport",
                        .shortcut = "Return",
                        .run = [&controller](const Json&) {
                            controller.locate(0);
                            return transportState(controller);
                        }});
    registry.addAction({.id = "transport.record",
                        .title = "Record",
                        .aliases = {"start recording"},
                        .keywords = {"take"},
                        .category = "Transport",
                        .shortcut = "R",
                        .run = [&controller](const Json&) {
                            controller.record();
                            return transportState(controller);
                        }});
    registry.addAction({.id = "transport.locate",
                        .title = "Go to position",
                        .aliases = {"locate", "move playhead"},
                        .category = "Transport",
                        .schema = {{"required", {"position"}}},
                        .run = [&controller](const Json& params) {
                            controller.locate(positionParam(params, "position", controller.project().document().tempo));
                            return transportState(controller);
                        }});
    registry.addAction({.id = "transport.setClockOut",
                        .title = "Send MIDI clock to port",
                        .aliases = {"midi clock", "sync out"},
                        .category = "Transport",
                        .schema = {{"required", {"port", "enabled"}}},
                        .run = [&controller](const Json& params) {
                            const auto port = param<std::string>(params, "port");
                            const auto enabled = param<bool>(params, "enabled");
                            const auto ports = controller.engine().midi().ports();
                            if (std::ranges::none_of(ports, [&](const engine::MidiPortInfo& info) {
                                    return info.id == port && !info.isInput;
                                })) {
                                throw AppError("no MIDI output port " + port + ", run devices.list for port ids");
                            }
                            std::vector<std::string> clockPorts = controller.project().document().clockPorts;
                            std::erase(clockPorts, port);
                            if (enabled) {
                                clockPorts.push_back(port);
                            }
                            controller.project().setClockPorts(std::move(clockPorts));
                            return Json{{"clockOut", controller.project().document().clockPorts}};
                        }});
}

engine::Session exportSession(ProjectController& controller, const Registry& registry, const Json& params) {
    engine::Session session = toSession(controller.project().document(), registry, controller.folder());
    const auto requested = optionalParam<std::vector<std::string>>(params, "tracks");
    if (requested) {
        std::vector<std::string> ids;
        for (const std::string& track : *requested) {
            ids.push_back(findTrackByIdOrName(controller.project(), track).value);
        }
        std::erase_if(session.tracks,
                      [&](const engine::Track& track) { return std::ranges::find(ids, track.id) == ids.end(); });
    }
    return session;
}

void registerExportActions(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "export.stems",
                        .title = "Export stems",
                        .aliases = {"bounce tracks", "render stems"},
                        .category = "Export",
                        .schema = {{"required", {"folder"}}, {"properties", {{"tracks", {{"type", "array"}}}}}},
                        .run = [&controller, &registry](const Json& params) {
                            const std::filesystem::path folder{param<std::string>(params, "folder")};
                            const auto files =
                                exportStems(exportSession(controller, registry, params),
                                            controller.project().document(), folder, controller.sampleRate());
                            Json written = Json::array();
                            for (const auto& file : files) {
                                written.push_back(file.string());
                            }
                            return Json{{"files", written},
                                        {"end", positionState(projectEnd(controller.project().document()),
                                                              controller.project().document().tempo)}};
                        }});
    registry.addAction({.id = "export.mix",
                        .title = "Export mix",
                        .aliases = {"bounce mix", "render mix"},
                        .category = "Export",
                        .schema = {{"required", {"path"}}},
                        .run = [&controller, &registry](const Json& params) {
                            const std::filesystem::path file{param<std::string>(params, "path")};
                            exportMix(toSession(controller.project().document(), registry, controller.folder()),
                                      controller.project().document(), file, controller.sampleRate());
                            return Json{{"file", file.string()}};
                        }});
}

}  // namespace

void registerProjectActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addStateProvider("project", [&controller] { return projectState(controller); });
    registry.addStateProvider("transport", [&controller] { return transportState(controller); });
    registry.addStateProvider("devices", [&controller] { return devicesState(controller); });

    registerProjectFileActions(registry, controller);
    registerTransportActions(registry, controller);
    registerTrackActions(registry, controller, nav);
    registerClipActions(registry, controller, nav);
    registerExportActions(registry, controller);
    registry.addAction({.id = "devices.list",
                        .title = "List audio and MIDI devices",
                        .aliases = {"show devices", "ports"},
                        .category = "Devices",
                        .run = [&controller](const Json&) { return devicesState(controller); }});
}

}  // namespace emerick::app
