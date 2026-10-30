// A Yamaha Expansion Manager store: one folder per pack, holding its voices as UVF XML files named by a GUID in braces
// and its waves as blobs named by their 40 hex digit SHA-1, which packs share. A pack's .index names the pack.

#include "store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <pugixml.hpp>
#include <ranges>
#include <sstream>
#include <tuple>

#include "wave.hpp"

namespace emerick::voices {

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 12> kNoteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr int kLowestOctave = -2;
constexpr std::size_t kWaveNameLength = 40;
constexpr double kFloorDb = -96.0;
constexpr double kDrumRateOffset = 0.534033;
constexpr double kOpenCutoff = 14000.0;
constexpr double kButterworthQ = 0.7071;
constexpr double kOpenLegacyCutoff = 112300.0;
// We calibrate the units of legacy curves and cutoff envelopes against Genos2 recordings.
constexpr double kCutoffCentsPerStep = 100.0;
constexpr double kTouchDbPerStep = 0.375;
constexpr double kCutoffSwingCents = 9600.0;
constexpr double kHoldSecondsPerUnit = 0.001;

bool isWaveName(std::string_view name) {
    return name.size() == kWaveNameLength && std::ranges::all_of(name, [](char letter) {
               return std::isxdigit(static_cast<unsigned char>(letter)) != 0;
           });
}

// The first line of .index whose first field is the pack's folder name holds the pack's name in its second field.
std::string packName(const fs::path& pack) {
    std::ifstream index(pack / ".index");
    std::string folder = pack.filename().string();
    for (std::string line; std::getline(index, line);) {
        const auto fields = line | std::views::split('\t') |
                            std::views::transform([](auto field) { return std::string_view(field); }) |
                            std::ranges::to<std::vector>();
        if (fields.size() > 1 && fields[0] == folder && !fields[1].empty()) {
            return std::string{fields[1]};
        }
    }
    return folder;
}

bool isOn(const pugi::xml_node& node, const char* child) { return std::string_view(node.child_value(child)) == "ON"; }

int note(const pugi::xml_node& node, const char* child, int fallback) {
    return yamahaNote(node.child_value(child)).value_or(fallback);
}

int number(const pugi::xml_node& node, const char* child, int fallback) {
    return node.child(child).text().as_int(fallback);
}

double decimal(const pugi::xml_node& node, const char* child) { return node.child(child).text().as_double(0.0); }

double tuneCents(const pugi::xml_node& node) { return 100.0 * decimal(node, "tuneCoarse") + decimal(node, "tuneFine"); }

struct Element {
    int keyLo = 0;
    int keyHi = 127;
    int velocityLo = 0;
    int velocityHi = 127;
    int keyShift = 0;
    double tuneCents = 0.0;
    double volumeDb = 0.0;
    double pan = 0.0;
    int waveform = -1;
    bool receivesNoteOff = true;
    int alternateGroup = 0;
    std::optional<Envelope> envelope;
    std::optional<LowPass> lowPass;
    std::optional<Curve> velocityDb;
    Lfo lfo;
};

double gainOf(double decibels) { return std::pow(10.0, decibels / 20.0); }

// Both envelope flavors store log10 of the seconds, a sixth per decade, from different origins.
double universalSeconds(double time) { return std::pow(10.0, 6.0 * (time - (2.0 / 3.0))); }

double drumSeconds(double rate) { return std::pow(10.0, 6.0 * (kDrumRateOffset - rate)); }

std::optional<Envelope> envelopeOf(const pugi::xml_node& amplitude) {
    const double attackDb = decimal(amplitude, "attackLevel");
    const double decay1Db = decimal(amplitude, "decay1Level");
    const double sustainDb = std::max(decimal(amplitude, "decay2Level"), kFloorDb);
    Envelope envelope{.attackGain = gainOf(attackDb), .decay1Gain = gainOf(decay1Db), .sustainGain = gainOf(sustainDb)};
    if (!amplitude.child("attackTime").empty()) {
        envelope.attackSeconds = universalSeconds(decimal(amplitude, "attackTime"));
        envelope.decay1Seconds = universalSeconds(decimal(amplitude, "decay1Time"));
        envelope.decay2Seconds = universalSeconds(decimal(amplitude, "decay2Time"));
        envelope.releaseSeconds = universalSeconds(decimal(amplitude, "releaseTime"));
        return envelope;
    }
    if (amplitude.child("attackRate").empty()) {
        return std::nullopt;
    }
    // A drum rate is the time a full 96 dB swing takes, so each segment takes its share of it.
    const auto seconds = [&](const char* rate, double fromDb, double toDb) {
        return drumSeconds(decimal(amplitude, rate)) * std::abs(toDb - fromDb) / -kFloorDb;
    };
    envelope.attackSeconds = drumSeconds(decimal(amplitude, "attackRate"));
    envelope.decay1Seconds = seconds("decay1Rate", attackDb, decay1Db);
    envelope.decay2Seconds = seconds("decay2Rate", decay1Db, sustainDb);
    envelope.releaseSeconds = seconds("releaseRate", sustainDb, kFloorDb);
    return envelope;
}

// A legacy curve table lists one step per velocity or key; one that names only a preset table holds no curve.
std::optional<Curve> curveOf(const pugi::xml_node& table, double perStep) {
    std::istringstream steps(table.child_value("table"));
    Curve curve{};
    std::size_t filled = 0;
    for (double step = 0.0; filled < curve.size() && steps >> step; ++filled) {
        curve.at(filled) = step * perStep;
    }
    return filled == curve.size() ? std::optional{curve} : std::nullopt;
}

// A cutoff envelope rate is the time a full swing takes, as a drum rate is, so the cutoff moves at a fixed speed.
std::optional<CutoffEnvelope> cutoffEnvelopeOf(const pugi::xml_node& filter) {
    const double depth = filter.child("EGDepth").text().as_double(100.0) / 100.0;
    const auto segment = [&](const char* level, const char* rate) {
        return CutoffSegment{.cents = depth * decimal(filter, level),
                             .centsPerSecond = kCutoffSwingCents / drumSeconds(decimal(filter, rate))};
    };
    const CutoffEnvelope envelope{
        .initialCents = depth * decimal(filter, "cegInitialLevel"),
        .holdSeconds = kHoldSecondsPerUnit * decimal(filter, "cegHoldTime"),
        .segments = {segment("ceg1stLevel", "ceg1stRate"), segment("ceg2ndLevel", "ceg2ndRate"),
                     segment("ceg3rdLevel", "ceg3rdRate"), segment("cegSustainLevel", "cegSustainRate"),
                     segment("cegReleaseLevel", "cegReleaseRate")}};
    const bool still =
        envelope.initialCents == 0.0 &&
        std::ranges::all_of(envelope.segments, [](double cents) { return cents == 0.0; }, &CutoffSegment::cents);
    if (!isOn(filter, "dcfCegEnable") || still) {
        return std::nullopt;
    }
    return envelope;
}

// We keep legacy voices at the Butterworth Q until we know the scale of their resonance.
std::optional<LowPass> legacyLowPassOf(const pugi::xml_node& filter) {
    const std::string_view type = filter.child_value("dcfFilterType");
    const LowPass lowPass{.hertz = decimal(filter, "dcfCutoff"),
                          .q = kButterworthQ,
                          .onePole = type.starts_with("LPF_6DBOCT"),
                          .velocityCents = curveOf(filter.child("dcfVelToCutoffTable"), kCutoffCentsPerStep),
                          .keyCents = curveOf(filter.child("dcfCutoffScalingTable"), kCutoffCentsPerStep),
                          .envelope = cutoffEnvelopeOf(filter)};
    const bool open =
        lowPass.hertz >= kOpenLegacyCutoff && !lowPass.velocityCents && !lowPass.keyCents && !lowPass.envelope;
    if (!type.starts_with("LPF") || lowPass.hertz <= 0.0 || open) {
        return std::nullopt;
    }
    return lowPass;
}

// The resonance is the peak in decibels halved, and 14 kHz means the filter is open.
std::optional<LowPass> lowPassOf(const pugi::xml_node& filter) {
    if (!filter.child("dcfFilterType").empty()) {
        return legacyLowPassOf(filter);
    }
    const double cutoff = filter.child("cutoffFrequency").text().as_double(kOpenCutoff);
    if (!std::string_view(filter.child_value("filterType")).starts_with("LPF") || cutoff <= 0.0 ||
        cutoff >= kOpenCutoff) {
        return std::nullopt;
    }
    return LowPass{.hertz = cutoff,
                   .q = std::max(kButterworthQ, std::pow(10.0, decimal(filter, "resonance") / 10.0)),
                   .onePole = false,
                   .velocityCents = std::nullopt,
                   .keyCents = std::nullopt,
                   .envelope = std::nullopt};
}

// Drum kits name the depths pmDepth, fmDepth and amDepth, other voices spell them out.
Lfo lfoOf(const pugi::xml_node& lfo) {
    return Lfo{.hertz = decimal(lfo, "frequency"),
               .delaySeconds = decimal(lfo, "delayTime") / 1000.0,
               .pitchCents = decimal(lfo, "pmDepth") + decimal(lfo, "pitchModulationDepth"),
               .cutoffCents = decimal(lfo, "fmDepth") + decimal(lfo, "filterModulationDepth"),
               .amplitudeDb = decimal(lfo, "amDepth") + decimal(lfo, "amplitudeModulationDepth")};
}

Element elementOf(const pugi::xml_node& node) {
    const pugi::xml_node blocks = node.child("blockComposition");
    const pugi::xml_node drumSetup = node.child("drumSetup");
    return Element{.keyLo = note(node, "noteLimitLo", 0),
                   .keyHi = note(node, "noteLimitHi", 127),
                   .velocityLo = number(node, "velocityLimitLo", 0),
                   .velocityHi = number(node, "velocityLimitHi", 127),
                   .keyShift = number(node, "noteShift", 0),
                   .tuneCents = tuneCents(blocks.child("pitchBlock")),
                   .volumeDb = decimal(node, "volume") + decimal(blocks.child("amplitudeBlock"), "volume"),
                   .pan = decimal(node, "pan"),
                   .waveform = number(blocks.child("oscillatorBlock"), "waveformNum", -1),
                   .receivesNoteOff = number(drumSetup, "rcvNoteOff", 1) != 0,
                   .alternateGroup = number(drumSetup, "alternateGroup", 0),
                   .envelope = envelopeOf(blocks.child("amplitudeBlock")),
                   .lowPass = lowPassOf(blocks.child("filterBlock")),
                   .velocityDb = curveOf(blocks.child("amplitudeBlock").child("touchCurveTable"), kTouchDbPerStep),
                   .lfo = lfoOf(blocks.child("lfoBlock"))};
}

std::string waveName(const pugi::xml_node& sample) {
    const char* file = sample.child_value("waveFile");
    return *file != '\0' ? file : sample.child_value("waveFilePath");
}

// A zone plays where its element's note and velocity limits overlap its key bank's, with the key bank's notes
// counted after the element's note shift.
std::optional<Zone> zoneOf(const Element& element, const pugi::xml_node& keybank, double commonDb) {
    Zone zone{.keyLo = std::max({element.keyLo, note(keybank, "noteLimitLo", 0) - element.keyShift, 0}),
              .keyHi = std::min({element.keyHi, note(keybank, "noteLimitHi", 127) - element.keyShift, 127}),
              .velocityLo = std::max(element.velocityLo, number(keybank, "velocityLimitLo", 0)),
              .velocityHi = std::min(element.velocityHi, number(keybank, "velocityLimitHi", 127)),
              .keyShift = element.keyShift,
              .rootKey = note(keybank, "centerNote", 60),
              .tuneCents = element.tuneCents + tuneCents(keybank),
              .fixedPitch = isOn(keybank, "fixedPitch"),
              .oneShot = isOn(keybank, "oneShot"),
              .receivesNoteOff = element.receivesNoteOff,
              .alternateGroup = element.alternateGroup,
              .sampleRate = keybank.child("samplingFrequency").text().as_double(44100.0),
              .gain = static_cast<float>(gainOf(commonDb + element.volumeDb + decimal(keybank, "volume"))),
              .velocityDb = element.velocityDb,
              .pan = static_cast<float>(std::clamp(element.pan + decimal(keybank, "pan"), -1.0, 1.0)),
              .leftWave = {},
              .rightWave = {},
              .envelope = element.envelope,
              .lowPass = element.lowPass,
              .lfo = element.lfo};
    for (const pugi::xml_node sample : keybank.children("sample")) {
        (std::string_view(sample.attribute("position").value()) == "RIGHT" ? zone.rightWave : zone.leftWave) =
            waveName(sample);
    }
    if (zone.keyLo > zone.keyHi || zone.velocityLo > zone.velocityHi || zone.leftWave.empty() || zone.sampleRate <= 0) {
        return std::nullopt;
    }
    return zone;
}

std::string contentsOf(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    std::string text(stream ? static_cast<std::size_t>(stream.tellg()) : 0U, '\0');
    stream.seekg(0);
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    return text;
}

// A voice file holds hundreds of kilobytes of tables, so we skip the ones with compressed waves before parsing them.
std::optional<Voice> voiceOf(const fs::path& file, const std::string& pack) {
    const std::string text = contentsOf(file);
    if (!text.starts_with("<?xml")) {
        return std::nullopt;
    }
    pugi::xml_document document;
    if (!document.load_buffer(text.data(), text.size())) {
        return std::nullopt;
    }
    const pugi::xml_node root = document.document_element();
    const pugi::xml_node information = root.child("information");
    if (!information.child("voicePcNum")) {
        return std::nullopt;
    }
    std::map<int, pugi::xml_node> waveforms;
    for (const pugi::xml_node waveform : root.children("waveform")) {
        waveforms[waveform.attribute("number").as_int(-1)] = waveform;
    }
    const pugi::xml_node common = root.child("voiceCommon");
    Voice voice{.pack = pack,
                .name = information.child_value("voiceName"),
                .address = {.msb = number(information, "voiceMsb", 0),
                            .lsb = number(information, "voiceLsb", 0),
                            .program = number(information, "voicePcNum", 0)},
                .zones = {}};
    for (const pugi::xml_node node : common.children("voiceElement")) {
        const Element element = elementOf(node);
        const auto waveform = waveforms.find(element.waveform);
        if (isOn(node, "mute") || waveform == waveforms.end()) {
            continue;
        }
        for (const pugi::xml_node keybank : waveform->second.children("keybank")) {
            if (auto zone = zoneOf(element, keybank, decimal(common, "volume"))) {
                voice.zones.push_back(std::move(*zone));
            }
        }
    }
    return voice;
}

}  // namespace

std::optional<int> yamahaNote(std::string_view name) {
    const auto found = std::ranges::find_if(kNoteNames | std::views::reverse,
                                            [&](std::string_view letter) { return name.starts_with(letter); });
    if (found == (kNoteNames | std::views::reverse).end()) {
        return std::nullopt;
    }
    const std::string octave{name.substr(found->size())};
    char* end = nullptr;
    const long value = std::strtol(octave.c_str(), &end, 10);
    if (octave.empty() || *end != '\0') {
        return std::nullopt;
    }
    const auto pitch = static_cast<int>(std::distance(kNoteNames.begin(), std::ranges::find(kNoteNames, *found)));
    return static_cast<int>(value - kLowestOctave) * 12 + pitch;
}

fs::path defaultStoreFolder() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");  // NOLINT(concurrency-mt-unsafe): we read it once while loading a store.
    return home != nullptr ? fs::path{home} / "Library/Application Support/Yamaha/Expansion Manager/Packs" : fs::path{};
#else
    return {};
#endif
}

Store loadStore(const fs::path& folder) {
    Store store;
    std::error_code error;
    std::vector<std::pair<fs::path, std::string>> voiceFiles;
    for (const fs::directory_entry& pack : fs::directory_iterator(folder, error)) {
        if (!pack.is_directory(error)) {
            continue;
        }
        const std::string name = packName(pack.path());
        for (const fs::directory_entry& item : fs::directory_iterator(pack.path(), error)) {
            const std::string file = item.path().filename().string();
            if (isWaveName(file)) {
                store.waves.emplace(file, item.path());
            } else if (file.starts_with('{')) {
                voiceFiles.emplace_back(item.path(), name);
            }
        }
    }
    std::unordered_map<std::string, bool> decodable;
    const auto playable = [&](const std::string& wave) {
        if (wave.empty()) {
            return true;
        }
        const auto known = decodable.find(wave);
        if (known != decodable.end()) {
            return known->second;
        }
        const auto path = store.waves.find(wave);
        return decodable[wave] = path != store.waves.end() && isDecodableWave(path->second);
    };
    for (const auto& [file, pack] : voiceFiles) {
        auto voice = voiceOf(file, pack);
        if (!voice) {
            continue;
        }
        std::erase_if(voice->zones,
                      [&](const Zone& zone) { return !playable(zone.leftWave) || !playable(zone.rightWave); });
        if (!voice->zones.empty()) {
            store.voices.push_back(std::move(*voice));
        }
    }
    std::ranges::sort(store.voices, {},
                      [](const Voice& voice) { return std::tie(voice.pack, voice.name, voice.address); });
    return store;
}

}  // namespace emerick::voices
