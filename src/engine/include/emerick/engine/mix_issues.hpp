#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/engine/audio_analysis.hpp"
#include "emerick/engine/window_meter.hpp"

namespace emerick::engine {

enum class OutputRole : std::uint8_t { track, bus, mix };

// One track, bus or mix measured over the whole range and over the windows every output shares.
struct InspectedOutput {
    OutputRole role = OutputRole::track;
    TrackLevels levels;
    std::vector<WindowLevels> windows;
};

enum class IssueKind : std::uint8_t { whine, beep, clipping, dc, imbalance, silent, harsh };
inline constexpr std::array<std::string_view, 7> kIssueKindNames{"whine",     "beep",   "clipping", "dc",
                                                                 "imbalance", "silent", "harsh"};

enum class IssueSeverity : std::uint8_t { low, medium, high };
inline constexpr std::array<std::string_view, 3> kIssueSeverityNames{"low", "medium", "high"};

struct IssueEvidence {
    std::string_view name;
    double value = 0;
};

struct MixIssue {
    IssueSeverity severity = IssueSeverity::low;
    IssueKind kind = IssueKind::whine;
    std::size_t output = 0;
    std::size_t firstWindow = 0;
    std::size_t lastWindow = 0;
    std::string detail;
    std::vector<IssueEvidence> evidence;
};

// Finds problems a listener would hear in the outputs, whose window i lasts windowBeats[i] beats, most severe first.
std::vector<MixIssue> inspectMix(std::span<const InspectedOutput> outputs, std::span<const double> windowBeats);

}  // namespace emerick::engine
