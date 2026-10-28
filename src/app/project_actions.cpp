#include "emerick/app/project_actions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"

namespace emerick::app {

namespace {

constexpr const char* kChaseMidiSetting = "midi.chase";
constexpr const char* kClockWhileStoppedSetting = "midi.clockWhileStopped";
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
                            if (std::isnan(tempo.bpm) || tempo.bpm < kMinBpm || tempo.bpm > kMaxBpm) {
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
    const char* extraShortcut;
    int bars;
};

constexpr std::array kBarSteps{
    BarStep{"transport.barBack", "Go back one bar", "rewind one bar", "Left", ",", -1},
    BarStep{"transport.barForward", "Go forward one bar", "forward one bar", "Right", ".", 1},
    BarStep{"transport.eightBarsBack", "Go back eight bars", "rewind eight bars", "Shift+Left", "Shift+,", -8},
    BarStep{"transport.eightBarsForward", "Go forward eight bars", "forward eight bars", "Shift+Right", "Shift+.", 8},
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
                            .extraShortcuts = {step.extraShortcut},
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
                        .schema = {{"required", {"at"}}},
                        .run = [&controller](const Json& params) {
                            controller.locate(positionParam(params, "at", controller.project().document().tempo));
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
    registry.addAction({.id = "transport.panic",
                        .title = "Send MIDI panic",
                        .description = "Stops clock ports and sends sustain off, all sound off and all notes off on "
                                       "every channel of every MIDI output in use",
                        .aliases = {"all notes off", "stuck notes", "reset midi"},
                        .category = "Transport",
                        .shortcut = "Ctrl+Shift+P",
                        .run =
                            [&controller](const Json&) {
                                controller.panic();
                                return transportState(controller);
                            },
                        .menus = {kTransportMenu}});
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
         .cancelGesture = [&project] { project.cancelGesture(); },
         .atomic = [&project](const std::function<void()>& edits) { project.atomic(edits); }});
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
    registry.addSetting(
        {.id = kPanLawSetting,
         .title = "Pan law",
         .aliases = {"pan curve", "center attenuation"},
         .category = "Settings / Audio",
         .type = SettingType::Choice,
         .defaultValue = "-3 dB compensated",
         .choices = kPanLaws | std::views::transform(&NamedPanLaw::name) | std::ranges::to<Json::array_t>(),
         .hint = "How much a centered track drops on each side, in playback, exports and meters",
         .location = "settings.audio"});
    registry.addSetting({.id = kChaseMidiSetting,
                         .title = "Chase MIDI on play",
                         .aliases = {"midi chase", "send program changes on play"},
                         .category = "Settings / MIDI",
                         .type = SettingType::Boolean,
                         .defaultValue = true,
                         .hint = "Send each MIDI track's last program, controllers and pitch bend before the playhead "
                                 "when playback starts",
                         .location = "settings.midi"});
    registry.addSetting({.id = kClockWhileStoppedSetting,
                         .title = "Send MIDI clock while stopped",
                         .aliases = {"free running clock", "clock always"},
                         .category = "Settings / MIDI",
                         .type = SettingType::Boolean,
                         .defaultValue = false,
                         .hint = "Keep clock running on clock ports, so a keyboard can start its Style at the project "
                                 "tempo",
                         .location = "settings.midi"});
    const auto playbackSettingsChanged = [&registry, &controller] {
        controller.panLawChanged(panLawFromSettings(registry));
        controller.midiSyncChanged(registry.settingValue(kChaseMidiSetting).get<bool>(),
                                   registry.settingValue(kClockWhileStoppedSetting).get<bool>());
    };
    QObject::connect(&registry, &Registry::settingsChanged, &controller, playbackSettingsChanged);
    playbackSettingsChanged();

    registerProjectFileActions(registry, controller);
    registerProjectEditActions(registry, controller);
    registerTransportActions(registry, controller);
    registerTrackActions(registry, controller, nav);
    registerAutomationActions(registry, controller, nav);
    registerClipActions(registry, controller, nav);
    registerMarkerActions(registry, controller, nav);
    registerCycleActions(registry, controller, nav);
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
                        .run =
                            [&controller](const Json&) {
                                controller.refreshDevices();
                                return devicesState(controller);
                            },
                        .menus = {kSettingsMenu}});
}

}  // namespace emerick::app
