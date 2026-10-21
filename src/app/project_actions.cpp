#include "emerick/app/project_actions.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"

namespace emerick::app {

namespace {

constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 400.0;
constexpr std::array kDenominators{1, 2, 4, 8, 16};

void registerProjectEditActions(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "project.undo",
                        .title = "Undo",
                        .category = "Project",
                        .shortcut = "Ctrl+Z",
                        .enabled = [&controller] { return controller.project().undoName().has_value(); },
                        .run =
                            [&controller](const Json&) {
                                controller.project().undo();
                                return projectState(controller);
                            },
                        .menus = {kEditMenu}});
    registry.addAction({.id = "project.redo",
                        .title = "Redo",
                        .category = "Project",
                        .shortcut = "Ctrl+Shift+Z",
                        .enabled = [&controller] { return controller.project().redoName().has_value(); },
                        .run =
                            [&controller](const Json&) {
                                controller.project().redo();
                                return projectState(controller);
                            },
                        .menus = {kEditMenu}});
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

struct BarStep {
    const char* id;
    const char* title;
    const char* alias;
    const char* shortcut;
    int bars;
};

constexpr std::array kBarSteps{
    BarStep{"transport.barBack", "Go back one bar", "rewind one bar", ",", -1},
    BarStep{"transport.barForward", "Go forward one bar", "forward one bar", ".", 1},
    BarStep{"transport.eightBarsBack", "Go back eight bars", "rewind eight bars", "Shift+,", -8},
    BarStep{"transport.eightBarsForward", "Go forward eight bars", "forward eight bars", "Shift+.", 8},
};

// Steps land on bar lines, so going back from inside a bar first returns to that bar's start, as in Logic.
core::Ticks barStep(core::Ticks position, core::Ticks ticksPerBar, int bars) {
    const core::Ticks bar = position / ticksPerBar;
    const bool onLine = position % ticksPerBar == 0;
    const core::Ticks target = bars < 0 && !onLine ? bar + bars + 1 : bar + bars;
    return std::max<core::Ticks>(0, target * ticksPerBar);
}

void registerTransportActions(Registry& registry, ProjectController& controller) {
    registry.addAction({.id = "transport.play",
                        .title = "Play",
                        .aliases = {"start playback"},
                        .category = "Transport",
                        .run =
                            [&controller](const Json&) {
                                controller.play();
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    registry.addAction({.id = "transport.togglePlay",
                        .title = "Play or pause",
                        .aliases = {"pause", "resume"},
                        .category = "Transport",
                        .shortcut = "Space",
                        .run =
                            [&controller](const Json&) {
                                if (controller.playing()) {
                                    controller.stop();
                                } else {
                                    controller.play();
                                }
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    // We return to the start on a stop while stopped, as Logic's stop button does.
    registry.addAction({.id = "transport.stop",
                        .title = "Stop",
                        .aliases = {"stop recording"},
                        .category = "Transport",
                        .run =
                            [&controller](const Json&) {
                                if (controller.playing()) {
                                    controller.stop();
                                } else {
                                    controller.locate(0);
                                }
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    registry.addAction({.id = "transport.gotoStart",
                        .title = "Go to beginning",
                        .aliases = {"rewind", "return to zero"},
                        .category = "Transport",
                        .shortcut = "Return",
                        .extraShortcuts = {"Home"},
                        .run =
                            [&controller](const Json&) {
                                controller.locate(0);
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    registry.addAction({.id = "transport.record",
                        .title = "Record",
                        .aliases = {"start recording"},
                        .keywords = {"take"},
                        .category = "Transport",
                        .shortcut = "R",
                        .run =
                            [&controller](const Json&) {
                                controller.record();
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    registry.addAction({.id = "transport.gotoEnd",
                        .title = "Go to end",
                        .aliases = {"project end", "fast forward to end"},
                        .category = "Transport",
                        .shortcut = "End",
                        .run =
                            [&controller](const Json&) {
                                controller.locate(controller.endTicks());
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
    for (const BarStep& step : kBarSteps) {
        registry.addAction({.id = step.id,
                            .title = step.title,
                            .aliases = {step.alias},
                            .category = "Transport",
                            .shortcut = step.shortcut,
                            .run =
                                [&controller, step](const Json&) {
                                    controller.locate(barStep(controller.positionTicks(),
                                                              controller.project().document().tempo.ticksPerBar(),
                                                              step.bars));
                                    return transportState(controller);
                                },
                            .menus = {kTransportMenu}});
    }
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
                            const auto& ports = controller.engine().midiPorts();
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
    registry.addAction({.id = "meters.resetClips",
                        .title = "Reset clip indicators",
                        .description = "Clears the clip lights of track, or of every track without it",
                        .aliases = {"clear clip", "clear overload"},
                        .category = "Transport",
                        .schema = {{"properties", {{"track", {{"type", "string"}}}}}},
                        .run = [&controller](const Json& params) {
                            if (params.contains("track")) {
                                controller.meters().resetClips(trackParam(controller.project(), params).value);
                            } else {
                                controller.meters().resetClips();
                            }
                            return Json{{"meters", controller.meters().state()}};
                        }});
}

}  // namespace

void registerProjectActions(Registry& registry, ProjectController& controller, Nav& nav) {
    registry.addStateProvider("project", [&controller] { return projectState(controller); });
    registry.addStateProvider("transport", [&controller] { return transportState(controller); });
    registry.addStateProvider("devices", [&controller] { return devicesState(controller); });
    model::Project& project = controller.project();
    registry.setUndoSteps(
        {.group = [&project](const std::string& name,
                             const std::function<void()>& edits) { project.group(name, edits); },
         .updateGesture = [&project](const std::string& name,
                                     const std::function<void()>& edits) { project.updateGesture(name, edits); },
         .endGesture = [&project] { return project.endGesture(); },
         .cancelGesture = [&project] { project.cancelGesture(); }});
    registry.addSetting({.id = "audio.latency",
                         .title = "Resulting latency",
                         .aliases = {"round trip", "delay"},
                         .category = "Settings / Audio",
                         .type = SettingType::String,
                         .defaultValue = "",
                         .readOnly = true,
                         .hint = "Input plus output latency the open devices report, buffer included",
                         .location = "settings.audio",
                         .value = [&controller] { return latencyText(controller.engine().layout()); }});

    registerProjectFileActions(registry, controller);
    registerProjectEditActions(registry, controller);
    registerTransportActions(registry, controller);
    registerTrackActions(registry, controller, nav);
    registerClipActions(registry, controller, nav);
    registerExportActions(registry, controller);
    registerMetronome(registry, controller, nav);
    registerProjectFolderActions(registry, controller, nav);
    registry.addAction({.id = "devices.list",
                        .title = "List audio and MIDI devices",
                        .aliases = {"show devices", "ports"},
                        .category = "Devices",
                        .run = [&controller](const Json&) { return devicesState(controller); }});
    registry.addAction({.id = "devices.refresh",
                        .title = "Refresh audio and MIDI devices",
                        .aliases = {"rescan devices", "find new devices"},
                        .category = "Devices",
                        .run = [&controller](const Json&) {
                            controller.refreshDevices();
                            return devicesState(controller);
                        }});
}

}  // namespace emerick::app
