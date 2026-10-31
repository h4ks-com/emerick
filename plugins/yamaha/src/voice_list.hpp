#pragma once

#include <filesystem>
#include <map>
#include <tuple>

#include "emerick/app/registry.hpp"
#include "gm_sounds.hpp"

namespace emerick::yamaha {

// A voice by bank MSB, bank LSB and 1-based program.
using VoiceKey = std::tuple<int, int, int>;

// The voice list the yamaha.voiceList setting names: the General MIDI category of each voice, and for drum kits the
// General MIDI note each Yamaha drum key plays.
struct VoiceList {
    std::map<VoiceKey, gm::Category> categories;
    std::map<VoiceKey, gm::DrumMap> drumMaps;
};

VoiceList readVoiceList(const std::filesystem::path& path);

struct CachedVoiceList {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
    VoiceList list;
};

// The voice list the setting names, read again when the setting or the file changes.
const VoiceList& voiceList(CachedVoiceList& cached, const app::Registry& registry);

}  // namespace emerick::yamaha
