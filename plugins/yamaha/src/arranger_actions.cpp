#include <QObject>
#include <QtLogging>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/core/midi.hpp"
#include "emerick/yamaha/profile.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

constexpr const char* kPluginId = "yamaha";
constexpr const char* kKeyboardName = "Digital Keyboard";

bool hasPort(const std::vector<engine::MidiPortInfo>& ports, const std::string& id, bool input) {
    return std::ranges::any_of(ports, [&](const auto& port) { return port.id == id && port.isInput == input; });
}

// The keyboard port is one the keyboard both sends on and listens to.
std::optional<std::string> keyboardPort(app::ProjectController& controller, const Json& params) {
    const auto ports = controller.engine().midi().ports();
    if (const auto requested = app::optionalParam<std::string>(params, "port")) {
        if (!hasPort(ports, *requested, true) || !hasPort(ports, *requested, false)) {
            throw AppError("no MIDI port " + *requested + " with both an input and an output, run devices.list");
        }
        return requested;
    }
    const auto duplex = [&](const engine::MidiPortInfo& port) {
        return port.isInput && hasPort(ports, port.id, false);
    };
    auto found = std::ranges::find_if(
        ports, [&](const auto& port) { return duplex(port) && port.name.contains(kKeyboardName); });
    if (found == ports.end()) {
        found = std::ranges::find_if(ports, duplex);
    }
    return found == ports.end() ? std::nullopt : std::optional{found->id};
}

std::string controllerPort(app::ProjectController& controller, const app::Registry& registry,
                           const std::string& keyboard) {
    const auto port = registry.settingValue(kControllerPortSetting).get<std::string>();
    return hasPort(controller.engine().midi().ports(), port, false) ? port : keyboard;
}

void addTrack(app::ProjectController& controller, model::Track track) {
    track.color = static_cast<int>(controller.project().document().tracks.size() % app::kTrackColors);
    controller.project().addTrack(std::move(track));
}

model::Track arrangerTrack(const app::Registry& registry, const char* kind, const std::string& keyboard,
                           model::MidiRoute out, Json data) {
    const app::TrackType& type = *registry.trackType(kind);
    model::Track track;
    track.kind = kind;
    track.name = type.title;
    track.icon = type.icon;
    track.midiIn = {.portId = keyboard, .channel = 0, .thru = false, .sysex = !keyboard.empty()};
    track.midiOut = std::move(out);
    track.data = std::move(data);
    return track;
}

// A Chords track sends block chords to the keyboard's chord detect channel, a Style track presses section buttons.
void addChartTracks(app::ProjectController& controller, const app::Registry& registry, const std::string& keyboard,
                    const Chart& chart) {
    const std::string sectionPort = keyboard.empty() ? keyboard : controllerPort(controller, registry, keyboard);
    addTrack(controller,
             arrangerTrack(registry, kChordKind, keyboard,
                           {keyboard, registry.settingValue(kChordChannelSetting).get<int>()}, Json(chart.chords)));
    addTrack(controller, arrangerTrack(registry, kStyleKind, keyboard,
                                       {sectionPort, registry.settingValue(kControllerChannelSetting).get<int>()},
                                       Json(chart.sections)));
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

// Voices go to the keyboard the first Chords track listens to.
std::optional<std::string> voicePort(const model::Document& document) {
    const auto chords = std::ranges::find(document.tracks, std::string{kChordKind}, &model::Track::kind);
    return chords == document.tracks.end() || chords->midiIn.portId.empty() ? std::nullopt
                                                                            : std::optional{chords->midiIn.portId};
}

// We send the table back to back; voiceMessages takes a gap for a keyboard that drops messages.
std::size_t sendVoices(app::ProjectController& controller, const std::string& port, const std::vector<Voice>& voices) {
    if (!hasPort(controller.engine().midi().ports(), port, false)) {
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
                        .run = [&controller](const Json& params) {
                            std::vector<Voice> voices;
                            try {
                                voices = loadVoices(app::param<std::string>(params, "path"));
                            } catch (const std::invalid_argument& error) {
                                throw AppError(error.what());
                            } catch (const std::runtime_error& error) {
                                throw AppError(error.what());
                            }
                            Json data = controller.project().document().pluginData.value(kPluginId, Json::object());
                            data["voices"] = voices;
                            controller.project().setPluginData(kPluginId, data, "load voice table");
                            return Json{{"voices", voices}};
                        }});
    registry.addAction({.id = "yamaha.voices.send",
                        .title = "Send voice table",
                        .aliases = {"send voices"},
                        .category = "Yamaha",
                        .schema = {{"properties", {{"port", {{"type", "string"}}}}}},
                        .run = [&controller](const Json& params) {
                            const model::Document& document = controller.project().document();
                            const auto port = app::optionalParam<std::string>(params, "port").or_else([&] {
                                return voicePort(document);
                            });
                            if (!port) {
                                throw AppError(
                                    "no Chords track names the keyboard, pass port or run "
                                    "yamaha.setupArranger");
                            }
                            const std::vector<Voice> voices = voicesOf(document);
                            if (voices.empty()) {
                                throw AppError("the project has no voice table, run yamaha.voices.load first");
                            }
                            return Json{{"port", *port}, {"messages", sendVoices(controller, *port, voices)}};
                        }});
    QObject::connect(&controller, &app::ProjectController::aboutToPlay, &controller, [&registry, &controller] {
        if (!registry.settingValue(kSendVoicesSetting).get<bool>()) {
            return;
        }
        try {
            const std::vector<Voice> voices = voicesOf(controller.project().document());
            const auto port = voicePort(controller.project().document());
            if (!voices.empty() && port) {
                sendVoices(controller, *port, voices);
            }
        } catch (const AppError& error) {
            qWarning("cannot send the voice table: %s", error.what());
        }
    });
}

}  // namespace

void registerArrangerActions(app::Registry& registry, app::ProjectController& controller) {
    registry.addAction(
        {.id = "yamaha.setupArranger",
         .title = "Set up arranger tracks",
         .aliases = {"set up keyboard", "record style parts"},
         .keywords = {"genos", "yamaha", "style"},
         .category = "Yamaha",
         .schema = {{"properties", {{"port", {{"type", "string"}}}}}},
         .run = [&registry, &controller](const Json& params) {
             const auto keyboard = keyboardPort(controller, params);
             if (!keyboard) {
                 throw AppError("no MIDI port with both an input and an output, connect the keyboard or pass port");
             }
             controller.project().group("set up arranger", [&] {
                 addChartTracks(controller, registry, *keyboard, {});
                 for (const Part& part : genos2Parts()) {
                     model::Track track;
                     track.kind = "midi";
                     track.name = part.name;
                     track.icon = part.icon;
                     track.midiIn = {.portId = *keyboard, .channel = part.channel, .thru = false, .sysex = false};
                     addTrack(controller, std::move(track));
                 }
                 std::vector<std::string> clockPorts = controller.project().document().clockPorts;
                 if (!std::ranges::contains(clockPorts, *keyboard)) {
                     clockPorts.push_back(*keyboard);
                     controller.project().setClockPorts(std::move(clockPorts));
                 }
             });
             return app::projectState(controller);
         }});
    registry.addAction(
        {.id = "yamaha.chart.import",
         .title = "Import chord chart from MIDI file",
         .aliases = {"import style take"},
         .category = "Yamaha",
         .schema = {{"required", {"path"}},
                    {"properties", {{"path", {{"type", "string"}}}, {"shiftBars", {{"type", "integer"}}}}}},
         .run = [&registry, &controller](const Json& params) {
             const std::filesystem::path path{app::param<std::string>(params, "path")};
             const int shiftBars = app::optionalParam<int>(params, "shiftBars").value_or(0);
             const Chart chart = chartFromTake(sysexOfMidiFile(path), controller.project().document().tempo,
                                               chartOptions(registry, shiftBars));
             const std::string keyboard = keyboardPort(controller, Json::object()).value_or("");
             controller.project().group("import chart " + path.filename().string(),
                                        [&] { addChartTracks(controller, registry, keyboard, chart); });
             return app::projectState(controller);
         }});
    registerVoiceActions(registry, controller);
}

}  // namespace emerick::yamaha
