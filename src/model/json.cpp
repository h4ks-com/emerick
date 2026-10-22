#include "emerick/model/json.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "emerick/core/overloaded.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::model {

// NOLINTBEGIN(modernize-use-constraints): the nlohmann macro expands to enable_if
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioRoute, deviceId, channels)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MidiRoute, portId, channel, thru, sysex)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MidiEvent, at, bytes)
// NOLINTEND(modernize-use-constraints)

namespace {

nlohmann::json sendToJson(const Send& send) {
    return {{"bus", send.bus.value}, {"levelDb", send.levelDb}, {"pre", send.pre}, {"enabled", send.enabled}};
}

Send sendFromJson(const nlohmann::json& json) {
    return {TrackId{json.at("bus")}, json.at("levelDb"), json.at("pre"), json.at("enabled")};
}

core::FadeShape fadeShapeFromJson(const nlohmann::json& json, const char* key) {
    const std::string id = json.value(key, std::string{core::fadeShapeId(core::FadeShape::linear)});
    const auto shape = core::fadeShapeFromId(id);
    if (!shape) {
        throw std::runtime_error("unknown fade shape: " + id);
    }
    return *shape;
}

nlohmann::json noteToJson(const Note& note) {
    return {{"id", note.id.value}, {"pitch", note.pitch},   {"velocity", note.velocity},
            {"start", note.start}, {"length", note.length}, {"channel", note.channel}};
}

Note noteFromJson(const nlohmann::json& json) {
    return {NoteId{json.at("id")}, json.at("pitch"),  json.at("velocity"),
            json.at("start"),      json.at("length"), json.at("channel")};
}

// Projects before format 4 keep their notes as note-ons and note-offs among the events.
MidiClip midiClipFromJson(const nlohmann::json& json) {
    MidiClip clip{ClipId{json.at("id")}, json.at("start"), json.at("length"), json.at("events"), {}};
    const auto notes = json.find("notes");
    if (notes == json.end()) {
        gatherNotes(clip);
    } else {
        clip.notes = *notes | std::views::transform(noteFromJson) | std::ranges::to<std::vector>();
    }
    return clip;
}

struct ClipType {
    std::string_view name;
    Clip (*fromJson)(const nlohmann::json& json);
};

// We index the table by the clip variant's index, so its rows follow the order of Clip's alternatives.
constexpr std::array kClipTypes{
    ClipType{"audio",
             [](const nlohmann::json& json) -> Clip {
                 return AudioClip{ClipId{json.at("id")},
                                  json.at("start"),
                                  json.at("length"),
                                  json.at("sourceOffset"),
                                  json.at("file"),
                                  json.at("gainDb"),
                                  json.at("fadeIn"),
                                  json.at("fadeOut"),
                                  fadeShapeFromJson(json, "fadeInShape"),
                                  fadeShapeFromJson(json, "fadeOutShape"),
                                  json.value("fadeInCurve", 0.0),
                                  json.value("fadeOutCurve", 0.0)};
             }},
    ClipType{"midi", [](const nlohmann::json& json) -> Clip { return midiClipFromJson(json); }},
};
static_assert(kClipTypes.size() == std::variant_size_v<Clip>);

nlohmann::json clipToJson(const Clip& clip) {
    nlohmann::json json =
        std::visit(core::Overloaded{[](const AudioClip& audio) -> nlohmann::json {
                                        return {{"id", audio.id.value},
                                                {"start", audio.start},
                                                {"length", audio.length},
                                                {"sourceOffset", audio.sourceOffset},
                                                {"file", audio.file},
                                                {"gainDb", audio.gainDb},
                                                {"fadeIn", audio.fadeIn},
                                                {"fadeOut", audio.fadeOut},
                                                {"fadeInShape", core::fadeShapeId(audio.fadeInShape)},
                                                {"fadeOutShape", core::fadeShapeId(audio.fadeOutShape)},
                                                {"fadeInCurve", audio.fadeInCurve},
                                                {"fadeOutCurve", audio.fadeOutCurve}};
                                    },
                                    [](const MidiClip& midi) -> nlohmann::json {
                                        return {{"id", midi.id.value},
                                                {"start", midi.start},
                                                {"length", midi.length},
                                                {"events", midi.events},
                                                {"notes", midi.notes | std::views::transform(noteToJson) |
                                                              std::ranges::to<nlohmann::json::array_t>()}};
                                    }},
                   clip);
    json["type"] = kClipTypes.at(clip.index()).name;
    return json;
}

Clip clipFromJson(const nlohmann::json& json) {
    const auto& type = json.at("type").get_ref<const std::string&>();
    const auto* const found = std::ranges::find(kClipTypes, type, &ClipType::name);
    if (found == kClipTypes.end()) {
        throw std::runtime_error("unknown clip type: " + type);
    }
    return found->fromJson(json);
}

nlohmann::json trackToJson(const Track& track) {
    nlohmann::json clips = nlohmann::json::array();
    for (const Clip& clip : track.clips) {
        clips.push_back(clipToJson(clip));
    }
    return {{"id", track.id.value},
            {"name", track.name},
            {"kind", track.kind},
            {"icon", track.icon},
            {"color", track.color},
            {"mute", track.mute},
            {"solo", track.solo},
            {"armed", track.armed},
            {"monitor", track.monitor},
            {"gainDb", track.gainDb},
            {"pan", track.pan},
            {"audioIn", track.audioIn},
            {"audioOut", track.audioOut},
            {"midiIn", track.midiIn},
            {"midiOut", track.midiOut},
            {"outputBus", track.outputBus.value},
            {"sends", track.sends | std::views::transform(sendToJson) | std::ranges::to<nlohmann::json::array_t>()},
            {"clips", std::move(clips)},
            {"data", track.data}};
}

Track trackFromJson(const nlohmann::json& json) {
    Track track{TrackId{json.at("id")},
                json.at("name"),
                json.at("kind"),
                json.at("icon"),
                json.at("color"),
                json.at("mute"),
                json.at("solo"),
                json.at("armed"),
                json.at("monitor"),
                json.value("gainDb", 0.0),
                json.value("pan", 0.0),
                json.at("audioIn"),
                json.at("audioOut"),
                json.at("midiIn"),
                json.at("midiOut"),
                TrackId{json.value("outputBus", std::string{})},
                json.value("sends", nlohmann::json::array()) | std::views::transform(sendFromJson) |
                    std::ranges::to<std::vector>(),
                {},
                json.at("data")};
    for (const auto& clip : json.at("clips")) {
        track.clips.push_back(clipFromJson(clip));
    }
    return track;
}

nlohmann::json markerToJson(const Marker& marker) {
    return {{"id", marker.id.value},
            {"name", marker.name},
            {"at", marker.at},
            {"color", marker.color ? nlohmann::json(*marker.color) : nlohmann::json(nullptr)}};
}

Marker markerFromJson(const nlohmann::json& json) {
    const auto& color = json.at("color");
    return {MarkerId{json.at("id")}, json.at("name"), json.at("at"),
            color.is_null() ? std::nullopt : std::optional<int>{color.get<int>()}};
}

}  // namespace

nlohmann::json toJson(const Document& document) {
    nlohmann::json tracks = nlohmann::json::array();
    for (const Track& track : document.tracks) {
        tracks.push_back(trackToJson(track));
    }
    return {{"format", kFormatVersion},
            {"tempo",
             {{"bpm", document.tempo.bpm},
              {"numerator", document.tempo.meter.numerator},
              {"denominator", document.tempo.meter.denominator}}},
            {"tracks", std::move(tracks)},
            {"markers",
             document.markers | std::views::transform(markerToJson) | std::ranges::to<nlohmann::json::array_t>()},
            {"clockPorts", document.clockPorts},
            {"pluginData", document.pluginData}};
}

Document documentFromJson(const nlohmann::json& json) {
    const auto& format = json.at("format");
    if (!format.is_number_integer() || format.get<int>() < 1 || format.get<int>() > kFormatVersion) {
        throw std::runtime_error("unsupported project format: " + format.dump());
    }
    const auto& tempo = json.at("tempo");
    Document document{{tempo.at("bpm"), {tempo.at("numerator"), tempo.at("denominator")}},
                      {},
                      json.value("markers", nlohmann::json::array()) | std::views::transform(markerFromJson) |
                          std::ranges::to<std::vector>(),
                      json.value("clockPorts", std::vector<std::string>{}),
                      json.value("pluginData", nlohmann::json::object())};
    for (const auto& track : json.at("tracks")) {
        document.tracks.push_back(trackFromJson(track));
    }
    return document;
}

void saveProject(const Document& document, const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder / "audio");
    const std::filesystem::path path = folder / "project.json";
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary);
    out << toJson(document).dump(2) << '\n';
    out.close();
    if (!out) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot write " + path.string());
    }
    std::filesystem::rename(temporary, path);
}

Document loadProject(const std::filesystem::path& folder) {
    std::ifstream in(folder / "project.json");
    if (!in) {
        throw std::runtime_error("cannot read " + (folder / "project.json").string());
    }
    return documentFromJson(nlohmann::json::parse(in));
}

}  // namespace emerick::model
