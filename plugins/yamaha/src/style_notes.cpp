#include "style_notes.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/yamaha/chart.hpp"
#include "emerick/yamaha/profile.hpp"

namespace emerick::yamaha {

namespace {

using app::AppError;
using app::Json;

constexpr std::size_t kFirstPart = 8;

std::string_view partName(std::size_t part) { return genos2Parts().at(kFirstPart + part).name; }

std::string sectionLabel(const arranger::StyleSection& section) {
    return sectionName({.at = 0, .kind = section.kind, .variation = section.variation, .bars = 0});
}

std::string joined(const std::vector<std::string>& names) {
    return names.empty()
               ? std::string{}
               : std::accumulate(std::next(names.begin()), names.end(), names.front(),
                                 [](const std::string& all, const std::string& name) { return all + ", " + name; });
}

std::vector<const arranger::StyleSection*> chosenSections(const arranger::Style& style,
                                                          const std::optional<std::string>& name) {
    auto all = style.sections | std::views::transform([](const auto& section) { return &section; }) |
               std::ranges::to<std::vector>();
    if (!name) {
        return all;
    }
    const auto found = std::ranges::find(all, *name, [](const auto* section) { return sectionLabel(*section); });
    if (found == all.end()) {
        const auto names = all | std::views::transform([](const auto* section) { return sectionLabel(*section); }) |
                           std::ranges::to<std::vector>();
        throw AppError("unknown section " + *name + ", use one of " + joined(names));
    }
    return {*found};
}

std::optional<std::size_t> chosenPart(const std::optional<std::string>& name) {
    if (!name) {
        return std::nullopt;
    }
    const auto parts = std::views::iota(std::size_t{0}, std::size_t{arranger::kParts});
    const auto found = std::ranges::find(parts, std::string_view{*name}, partName);
    if (found == parts.end()) {
        const auto names = parts | std::views::transform([](std::size_t part) { return std::string{partName(part)}; }) |
                           std::ranges::to<std::vector>();
        throw AppError("unknown part " + *name + ", use one of " + joined(names));
    }
    return *found;
}

Json voiceJson(const std::optional<arranger::PartVoice>& voice, const VoiceList& list,
               const std::optional<std::string>& packName) {
    if (!voice) {
        return nullptr;
    }
    const int program = voice->program + 1;
    Json result{{"msb", voice->bankMsb}, {"lsb", voice->bankLsb}, {"program", program}};
    if (packName) {
        result["name"] = *packName;
    } else if (voice->bankMsb == 0 && voice->bankLsb == 0) {
        result["name"] = gm::kSounds.at(voice->program);
    }
    if (const auto found = list.categories.find({voice->bankMsb, voice->bankLsb, program});
        found != list.categories.end()) {
        result["category"] = found->second.name;
    }
    return result;
}

// General MIDI plays a drum key as the voice list maps it, drops it at -1, and plays it unchanged when the list maps
// no key of the kit.
Json drumNotes(const std::map<int, int>& notes, const VoiceList& list, const arranger::PartVoice& voice) {
    const auto map = list.drumMaps.find({voice.bankMsb, voice.bankLsb, voice.program + 1});
    return notes | std::views::transform([&](const auto& entry) {
               const auto [key, count] = entry;
               const int gmNote = map == list.drumMaps.end() ? key : map->second.at(static_cast<std::size_t>(key));
               Json note{{"key", key}, {"count", count}, {"gmNote", gmNote}};
               if (const auto name = gm::drumName(gmNote)) {
                   note["gmName"] = *name;
               }
               return note;
           }) |
           std::ranges::to<std::vector<Json>>();
}

Json melodicNotes(const std::map<int, int>& notes) {
    return notes | std::views::transform([](const auto& entry) {
               return Json{{"note", entry.first}, {"count", entry.second}};
           }) |
           std::ranges::to<std::vector<Json>>();
}

Json partJson(std::size_t part, const std::map<int, int>& notes, const VoiceList& list,
              const std::optional<arranger::PartVoice>& voice, const std::optional<std::string>& packName) {
    const int count = std::ranges::fold_left(notes | std::views::values, 0, std::plus{});
    Json result{{"part", partName(part)},
                {"channel", genos2Parts().at(kFirstPart + part).channel},
                {"voice", voiceJson(voice, list, packName)},
                {"noteCount", count}};
    if (voice && gm::isDrumBank(voice->bankMsb)) {
        result["notes"] = drumNotes(notes, list, *voice);
        return result;
    }
    result["notes"] = melodicNotes(notes);
    if (!notes.empty()) {
        result["low"] = notes.begin()->first;
        result["high"] = notes.rbegin()->first;
    }
    return result;
}

}  // namespace

Json styleNotes(const arranger::Style& style, const VoiceList& list, const PackVoiceNames& packNames,
                const std::optional<std::string>& section, const std::optional<std::string>& part) {
    const std::vector<const arranger::StyleSection*> sections = chosenSections(style, section);
    const std::optional<std::size_t> onlyPart = chosenPart(part);
    const double ticksPerBar =
        style.ticksPerQuarter * 4.0 * style.meter.numerator / std::max(1, style.meter.denominator);
    Json result = Json::array();
    for (const arranger::StyleSection* playing : sections) {
        const auto notes = arranger::partNotes(*playing);
        Json parts = Json::array();
        for (std::size_t index = 0; index < arranger::kParts; ++index) {
            const bool asked = onlyPart == index;
            if (asked || (!onlyPart && !notes.at(index).empty())) {
                parts.push_back(partJson(index, notes.at(index), list, style.voices.at(index), packNames.at(index)));
            }
        }
        result.push_back({{"section", sectionLabel(*playing)},
                          {"bars", static_cast<double>(playing->length) / ticksPerBar},
                          {"parts", parts}});
    }
    return {{"sections", result}, {"message", std::to_string(result.size()) + " sections"}};
}

}  // namespace emerick::yamaha
