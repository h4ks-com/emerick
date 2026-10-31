#include "voice_list.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "emerick/yamaha/profile.hpp"
#include "style_library.hpp"

namespace emerick::yamaha {

namespace {

std::optional<int> numberIn(std::string_view field, int first, int last) {
    const std::optional<int> value = wholeNumber(trim(field));
    return value && *value >= first && *value <= last ? value : std::nullopt;
}

void readDrumNote(VoiceList& list, std::span<const std::string_view, 5> fields) {
    const auto msb = numberIn(fields[0], 0, 127);
    const auto lsb = numberIn(fields[1], 0, 127);
    const auto program = numberIn(fields[2], 1, 128);
    const auto note = numberIn(fields[3], 0, 127);
    const auto played = numberIn(fields[4], -1, 127);
    if (msb && lsb && program && note && played) {
        auto [map, added] = list.drumMaps.try_emplace({*msb, *lsb, *program});
        if (added) {
            map->second.fill(-1);
        }
        map->second.at(static_cast<std::size_t>(*note)) = *played;
    }
}

}  // namespace

VoiceList readVoiceList(const std::filesystem::path& path) {
    VoiceList list;
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);) {
        const auto fields = line | std::views::split(',') |
                            std::views::transform([](auto field) { return std::string_view(field); }) |
                            std::ranges::to<std::vector>();
        if (fields.size() == 5) {
            readDrumNote(list, std::span<const std::string_view, 5>(fields));
        }
        if (fields.size() != 4) {
            continue;
        }
        const auto msb = numberIn(fields[0], 0, 127);
        const auto lsb = numberIn(fields[1], 0, 127);
        const auto program = numberIn(fields[2], 1, 128);
        const auto* const category = std::ranges::find(gm::kCategories, trim(fields[3]), &gm::Category::name);
        if (msb && lsb && program && category != gm::kCategories.end()) {
            list.categories[{*msb, *lsb, *program}] = *category;
        }
    }
    return list;
}

const VoiceList& voiceList(CachedVoiceList& cached, const app::Registry& registry) {
    const std::filesystem::path path = registry.settingValue(kVoiceListSetting).get<std::string>();
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        cached = {};
    } else if (path != cached.path || modified != cached.modified) {
        cached = {.path = path, .modified = modified, .list = readVoiceList(path)};
    }
    return cached.list;
}

}  // namespace emerick::yamaha
