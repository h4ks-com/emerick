#include <QObject>
#include <QtLogging>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/engine/audio_backend.hpp"
#include "emerick/yamaha/profile.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

constexpr const char* kPluginId = "yamaha";
constexpr const char* kKeyboardName = "Digital Keyboard";
constexpr const char* kKeyboardAudioName = "Keyboard Audio";

bool hasPort(const std::vector<engine::MidiPortInfo>& ports, const std::string& id, bool input) {
    return std::ranges::any_of(ports, [&](const auto& port) { return port.id == id && port.isInput == input; });
}

// The keyboard port is one the keyboard both sends on and listens to, other than its External Controller port.
std::optional<std::string> keyboardPort(const app::Registry& registry, app::ProjectController& controller,
                                        const Json& params) {
    const auto& ports = controller.engine().midiPorts();
    if (const auto requested = app::optionalParam<std::string>(params, "port")) {
        if (!hasPort(ports, *requested, true) || !hasPort(ports, *requested, false)) {
            throw AppError("no MIDI port " + *requested + " with both an input and an output, run devices.list");
        }
        return requested;
    }
    const auto controllerPort = registry.settingValue(kControllerPortSetting).get<std::string>();
    const auto duplex = [&](const engine::MidiPortInfo& port) {
        return port.isInput && hasPort(ports, port.id, false);
    };
    auto found = std::ranges::find_if(ports, [&](const auto& port) {
        return duplex(port) && port.name.contains(kKeyboardName) && port.id != controllerPort;
    });
    if (found == ports.end()) {
        found = std::ranges::find_if(ports, duplex);
    }
    return found == ports.end() ? std::nullopt : std::optional{found->id};
}

void addTrack(app::ProjectController& controller, model::Track track) {
    track.color = static_cast<int>(controller.project().document().tracks.size() % app::kTrackColors);
    controller.project().addTrack(std::move(track));
}

// The keyboard's other port, which feeds its Song parts and so plays the recorded parts back as they sounded.
std::string partsPort(app::ProjectController& controller, const std::string& keyboard) {
    const auto& ports = controller.engine().midiPorts();
    const auto found = std::ranges::find_if(ports, [&](const engine::MidiPortInfo& port) {
        return !port.isInput && port.id != keyboard && port.name.contains(kKeyboardName);
    });
    return found == ports.end() ? keyboard : found->id;
}

// The Style track presses section buttons on the External Controller port, or on the parts port without one.
model::MidiRoute controllerRoute(const app::Registry& registry, app::ProjectController& controller,
                                 const std::string& fallback) {
    const auto port = registry.settingValue(kControllerPortSetting).get<std::string>();
    return {.portId = hasPort(controller.engine().midiPorts(), port, false) ? port : fallback,
            .channel = registry.settingValue(kControllerChannelSetting).get<int>(),
            .thru = false,
            .sysex = false};
}

// The keyboard reads Chord and Section Control sysex on its own port whatever its Receive channels are.
model::Track arrangerTrack(const app::Registry& registry, const char* kind, const std::string& keyboard, Json data) {
    const app::TrackType& type = *registry.trackType(kind);
    model::Track track;
    track.kind = kind;
    track.name = type.title;
    track.icon = type.icon;
    track.midiIn = {.portId = keyboard, .channel = 0, .thru = false, .sysex = !keyboard.empty()};
    track.midiOut = {.portId = keyboard, .channel = 0, .thru = false, .sysex = false};
    track.data = std::move(data);
    return track;
}

void addChartTracks(app::ProjectController& controller, const app::Registry& registry, const std::string& keyboard,
                    const Chart& chart, const model::MidiRoute& styleOut) {
    addTrack(controller, arrangerTrack(registry, kChordKind, keyboard, Json(chart.chords)));
    model::Track style = arrangerTrack(registry, kStyleKind, keyboard, Json(chart.sections));
    style.midiOut = styleOut;
    addTrack(controller, std::move(style));
}

// Armed part tracks keep thru off, so the keyboard never hears its own notes echoed back.
model::Track partTrack(const Part& part, const std::string& keyboard, const std::string& output) {
    model::Track track;
    track.kind = "midi";
    track.name = part.name;
    track.icon = part.icon;
    track.armed = true;
    track.midiIn = {.portId = keyboard, .channel = part.channel, .thru = false, .sysex = false};
    track.midiOut = {.portId = output, .channel = part.channel, .thru = false, .sysex = false};
    return track;
}

std::optional<std::string> keyboardAudio(app::ProjectController& controller) {
    const auto& devices = controller.engine().audioDevices();
    const auto found = std::ranges::find_if(devices, [&](const engine::AudioDeviceInfo& device) {
        return device.name.contains(kKeyboardName) && device.inputChannels >= 2 &&
               !engine::unsupportedRate(device, controller.sampleRate());
    });
    return found == devices.end() ? std::nullopt : std::optional{found->id};
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw AppError("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// We read the sysex of the first track that has any, the track the keyboard records its Style control on.
std::vector<RawEvent> sysexOfMidiFile(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = readFile(path);
    libremidi::reader reader(true);
    if (reader.parse(bytes.data(), bytes.size()) < libremidi::reader::complete) {
        throw AppError("cannot parse " + path.string() + " as a MIDI file");
    }
    for (const auto& track : reader.tracks) {
        std::vector<RawEvent> events;
        for (const auto& event : track) {
            if (!event.m.bytes.empty() && event.m.bytes.front() == core::midi::kSysexStart) {
                const auto at = std::llround(static_cast<double>(event.tick) * core::kTicksPerQuarter /
                                             static_cast<double>(reader.ticksPerBeat));
                events.push_back({at, {event.m.bytes.begin(), event.m.bytes.end()}});
            }
        }
        if (!events.empty()) {
            return events;
        }
    }
    throw AppError(path.string() + " holds no sysex, record it with the keyboard's Style playing");
}

std::vector<Voice> voicesOf(const model::Document& document) {
    const Json data = document.pluginData.value(kPluginId, Json::object());
    try {
        return data.value("voices", Json::array()).get<std::vector<Voice>>();
    } catch (const Json::exception& error) {
        throw AppError(std::string{"the project's voice table is damaged: "} + error.what());
    } catch (const std::invalid_argument& error) {
        throw AppError(std::string{"the project's voice table is damaged: "} + error.what());
    }
}

// A voice's bank, volume and pan changes on the Style's button channel would press section buttons, so we leave that
// voice out.
std::vector<Voice> voicesBesideButtons(const model::Document& document, const std::string& port) {
    std::vector<Voice> voices = voicesOf(document);
    if (const auto buttons = styleButtons(document); buttons && buttons->portId == port) {
        std::erase_if(voices, [&](const Voice& voice) { return voice.channel == buttons->channel; });
    }
    return voices;
}

// Voices go where the parts recorded from the first Chords track's keyboard play back, so they reach the parts that
// play them.
std::optional<std::string> voicePort(const model::Document& document) {
    const auto chords = std::ranges::find(document.tracks, std::string{kChordKind}, &model::Track::kind);
    if (chords == document.tracks.end() || chords->midiIn.portId.empty()) {
        return std::nullopt;
    }
    const auto part = std::ranges::find_if(document.tracks, [&](const model::Track& track) {
        return track.kind == "midi" && track.midiIn.portId == chords->midiIn.portId && track.midiIn.channel != 0 &&
               !track.midiOut.portId.empty();
    });
    return part == document.tracks.end() ? chords->midiIn.portId : part->midiOut.portId;
}

// We send the table back to back; voiceMessages takes a gap for a keyboard that drops messages.
std::size_t sendVoices(app::ProjectController& controller, const std::string& port, const std::vector<Voice>& voices) {
    if (!hasPort(controller.engine().midiPorts(), port, false)) {
        throw AppError("no MIDI output port " + port + ", run devices.list for port ids");
    }
    const auto messages = voiceMessages(voices, 0, 0);
    try {
        const auto output = controller.engine().midi().openOutput(port);
        for (const RawEvent& message : messages) {
            output->send(message.bytes, 0);
        }
    } catch (const std::invalid_argument& error) {
        throw AppError(error.what());
    } catch (const std::runtime_error& error) {
        throw AppError(error.what());
    }
    return messages.size();
}

void registerVoiceActions(app::Registry& registry, app::ProjectController& controller) {
    registry.addAction({.id = "yamaha.voices.load",
                        .title = "Load voice table",
                        .aliases = {"style voices"},
                        .category = "Yamaha",
                        .schema = {{"required", {"path"}}, {"properties", {{"path", {{"type", "string"}}}}}},
                        .run =
                            [&registry, &controller](const Json& params) {
                                std::vector<Voice> voices;
                                try {
                                    voices = loadVoices(registry.checkedPath(app::param<std::string>(params, "path"),
                                                                             app::PathAccess::Read));
                                } catch (const std::invalid_argument& error) {
                                    throw AppError(error.what());
                                } catch (const std::runtime_error& error) {
                                    throw AppError(error.what());
                                }
                                Json data = controller.project().document().pluginData.value(kPluginId, Json::object());
                                data["voices"] = voices;
                                controller.project().setPluginData(kPluginId, data, "load voice table");
                                return Json{{"voices", voices}};
                            },
                        .menus = {kYamahaMenu},
                        .dialog = app::FileDialog{.param = "path", .nameFilter = "Voice tables (*.txt)"}});
    registry.addAction({.id = "yamaha.voices.send",
                        .title = "Send voice table",
                        .aliases = {"send voices"},
                        .category = "Yamaha",
                        .schema = {{"properties", {{"port", {{"type", "string"}}}}}},
                        .run =
                            [&controller](const Json& params) {
                                const model::Document& document = controller.project().document();
                                const auto port = app::optionalParam<std::string>(params, "port").or_else([&] {
                                    return voicePort(document);
                                });
                                if (!port) {
                                    throw AppError(
                                        "no Chords track names the keyboard, pass port or run "
                                        "yamaha.setupArranger");
                                }
                                const std::vector<Voice> voices = voicesBesideButtons(document, *port);
                                if (voices.empty()) {
                                    throw AppError("the project has no voice table, run yamaha.voices.load first");
                                }
                                return Json{{"port", *port}, {"messages", sendVoices(controller, *port, voices)}};
                            },
                        .menus = {kYamahaMenu}});
    QObject::connect(&controller, &app::ProjectController::aboutToPlay, &controller, [&registry, &controller] {
        if (!registry.settingValue(kSendVoicesSetting).get<bool>()) {
            return;
        }
        try {
            const model::Document& document = controller.project().document();
            if (const auto port = voicePort(document)) {
                if (const std::vector<Voice> voices = voicesBesideButtons(document, *port); !voices.empty()) {
                    sendVoices(controller, *port, voices);
                }
            }
        } catch (const AppError& error) {
            qWarning("cannot send the voice table: %s", error.what());
        }
    });
}

}  // namespace

void registerArrangerActions(app::Registry& registry, app::ProjectController& controller, KeyboardCheck& check) {
    registry.addAction(
        {.id = "yamaha.setupArranger",
         .title = "Set up arranger tracks",
         .aliases = {"set up keyboard", "record style parts"},
         .keywords = {"genos", "yamaha", "style"},
         .category = "Yamaha",
         .schema = {{"properties", {{"port", {{"type", "string"}}}}}},
         .run =
             [&registry, &controller, &check](const Json& params) {
                 const auto keyboard = keyboardPort(registry, controller, params);
                 if (!keyboard) {
                     throw AppError("no MIDI port with both an input and an output, connect the keyboard or pass port");
                 }
                 const std::string parts = partsPort(controller, *keyboard);
                 controller.project().group("set up arranger", [&] {
                     const std::size_t first = controller.project().document().tracks.size();
                     addChartTracks(controller, registry, *keyboard, {}, controllerRoute(registry, controller, parts));
                     for (const Part& part : genos2Parts()) {
                         addTrack(controller, partTrack(part, *keyboard, parts));
                     }
                     if (const auto audio = keyboardAudio(controller)) {
                         registry.run("track.addAudio", {{"name", kKeyboardAudioName},
                                                         {"audioIn", {{"device", *audio}, {"channels", {1, 2}}}}});
                     }
                     const auto& tracks = controller.project().document().tracks;
                     const auto added = tracks | std::views::drop(first) | std::views::transform(&model::Track::id) |
                                        std::ranges::to<std::vector>();
                     for (const model::TrackId& id : added) {
                         app::editTrack(controller, id, "arm track", [](model::Track& track) { track.armed = true; });
                     }
                     std::vector<std::string> clockPorts = controller.project().document().clockPorts;
                     if (!std::ranges::contains(clockPorts, *keyboard)) {
                         clockPorts.push_back(*keyboard);
                         controller.project().setClockPorts(std::move(clockPorts));
                     }
                 });
                 check.listen(*keyboard);
                 Json state = app::projectState(controller);
                 state["hints"] = check.hints();
                 return state;
             },
         .menus = {kYamahaMenu}});
    registry.addAction({.id = "yamaha.keyboard.check",
                        .title = "Check keyboard settings",
                        .aliases = {"keyboard hints", "what the keyboard does not send"},
                        .category = "Yamaha",
                        .run = [&check](const Json&) { return Json{{"hints", check.hints()}}; },
                        .menus = {kYamahaMenu}});
    registry.addAction(
        {.id = "yamaha.chart.import",
         .title = "Import chord chart from MIDI file",
         .aliases = {"import style take"},
         .category = "Yamaha",
         .schema = {{"required", {"path"}},
                    {"properties", {{"path", {{"type", "string"}}}, {"shiftBars", {{"type", "integer"}}}}}},
         .run =
             [&registry, &controller](const Json& params) {
                 const std::filesystem::path path =
                     registry.checkedPath(app::param<std::string>(params, "path"), app::PathAccess::Read);
                 const int shiftBars = app::optionalParam<int>(params, "shiftBars").value_or(0);
                 const Chart chart = chartFromTake(sysexOfMidiFile(path), controller.project().document().tempo,
                                                   chartOptions(registry, shiftBars));
                 const std::string keyboard = keyboardPort(registry, controller, Json::object()).value_or("");
                 const model::MidiRoute styleOut =
                     controllerRoute(registry, controller, partsPort(controller, keyboard));
                 controller.project().group("import chart " + path.filename().string(),
                                            [&] { addChartTracks(controller, registry, keyboard, chart, styleOut); });
                 return app::projectState(controller);
             },
         .menus = {kYamahaMenu},
         .dialog = app::FileDialog{.param = "path", .nameFilter = "MIDI files (*.mid *.midi)"}});
    registerVoiceActions(registry, controller);
}

}  // namespace emerick::yamaha
