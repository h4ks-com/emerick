#pragma once

#include <cmath>
#include <fstream>
#include <iterator>
#include <libremidi/reader.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "emerick/yamaha/plugin.hpp"
#include "emerick/yamaha/raw_event.hpp"
#include "project_rig.hpp"

namespace emerick::yamaha::test {

using app::Json;
using Bytes = std::vector<std::uint8_t>;

inline std::string dataPath(const std::string& name) { return std::string(TEST_DATA_DIR) + "/" + name; }

inline std::string readText(const std::string& name) {
    std::ifstream file(dataPath(name));
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// The chord and section sysex of the recorded take in tests/data, in core ticks.
inline std::vector<RawEvent> takeSysex() {
    std::ifstream file(dataPath("take.mid"), std::ios::binary);
    const Bytes bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    libremidi::reader reader(true);
    reader.parse(bytes.data(), bytes.size());
    std::vector<RawEvent> events;
    for (const auto& track : reader.tracks) {
        for (const auto& event : track) {
            if (event.m.bytes.front() == 0xF0) {
                const auto at = std::llround(static_cast<double>(event.tick) * core::kTicksPerQuarter /
                                             static_cast<double>(reader.ticksPerBeat));
                events.push_back({at, Bytes(event.m.bytes.begin(), event.m.bytes.end())});
            }
        }
        if (!events.empty()) {
            break;
        }
    }
    return events;
}

// A project rig with the plugin registered.
struct YamahaRig : app::test::ProjectRig {
    explicit YamahaRig(const std::vector<std::string>& midiPorts = {app::test::kMidiPort},
                       double sampleRate = app::test::kRate)
        : ProjectRig(midiPorts, sampleRate), chordInput(registerWith(registry, *controller, nav)) {}

    std::unique_ptr<ChordInput> chordInput;

    [[nodiscard]] const model::Track& track(const std::string& id) const {
        return *controller->project().track(model::TrackId{id});
    }

    [[nodiscard]] std::string addTrack(const char* kind) {
        model::Track added;
        added.kind = kind;
        added.name = kind;
        const std::string id = added.id.value;
        controller->project().addTrack(std::move(added));
        return id;
    }
};

}  // namespace emerick::yamaha::test
