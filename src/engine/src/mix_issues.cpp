#include "emerick/engine/mix_issues.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

#include "meter_detail.hpp"

namespace emerick::engine {

namespace {

constexpr double kWhineTonality = 0.6;
constexpr double kWhineStrongTonality = 0.85;
constexpr std::size_t kWhineMinWindows = 4;
constexpr double kBeepTonality = 0.4;
constexpr double kBeepMinHz = 2000;
constexpr double kBeepMinCrestDb = 10;
constexpr int kBeepMinOnsets = 2;
constexpr std::size_t kBeepMinWindows = 2;
// A tone stays the same tone while its peak moves less than this share of its frequency.
constexpr double kStableHzShare = 0.03;
constexpr double kDcOffset = 0.01;
constexpr double kLoudOverMedianDb = 8;
constexpr double kQuietUnderMedianDb = -15;
constexpr double kHarshShareJump = 0.2;
constexpr double kHarshShareRatio = 1.5;
constexpr std::size_t kHarshMinWindows = 2;
constexpr std::size_t kHarshMinActiveWindows = 4;
constexpr std::size_t kFirstHighBand = 3;

struct Run {
    std::size_t first = 0;
    std::size_t last = 0;

    [[nodiscard]] std::size_t length() const { return last - first + 1; }
};

bool active(const WindowLevels& window) { return std::isfinite(window.activeRmsDb); }

double highShare(const WindowLevels& window) {
    return std::ranges::fold_left(window.bandShare | std::views::drop(kFirstHighBand), 0.0, std::plus{});
}

bool sameTone(const WindowLevels& first, const WindowLevels& window) {
    return std::abs(window.tonalPeakHz - first.tonalPeakHz) <= kStableHzShare * first.tonalPeakHz;
}

// The runs of consecutive windows that pass, each window keeping with the first window of its run.
template <typename Passes, typename Keeps>
std::vector<Run> runsOf(const std::vector<WindowLevels>& windows, Passes passes, Keeps keeps, std::size_t minLength) {
    std::vector<Run> runs;
    for (std::size_t first = 0; first < windows.size(); ++first) {
        if (!passes(windows[first])) {
            continue;
        }
        std::size_t last = first;
        while (last + 1 < windows.size() && passes(windows[last + 1]) && keeps(windows[first], windows[last + 1])) {
            ++last;
        }
        if (last - first + 1 >= minLength) {
            runs.push_back({first, last});
        }
        first = last;
    }
    return runs;
}

template <typename Measure>
double runMedian(const std::vector<WindowLevels>& windows, const Run& run, Measure measure) {
    return median(std::views::iota(run.first, run.last + 1) |
                  std::views::transform([&](std::size_t index) { return measure(windows[index], index); }) |
                  std::ranges::to<std::vector>());
}

double runMax(const std::vector<WindowLevels>& windows, const Run& run, double WindowLevels::* field) {
    return std::ranges::max(std::span{windows}.subspan(run.first, run.length()), {}, field).*field;
}

class Finder {
public:
    Finder(std::span<const InspectedOutput> outputs, std::span<const double> windowBeats)
        : outputs_(outputs), beats_(windowBeats) {}

    std::vector<MixIssue> find() {
        for (std::size_t output = 0; output < outputs_.size(); ++output) {
            if (!std::isfinite(outputs_[output].levels.tone.activeRmsDb)) {
                silent(output);
                continue;
            }
            whines(output);
            beeps(output);
            clipping(output);
            dc(output);
            harsh(output);
        }
        imbalance();
        std::ranges::stable_sort(issues_, std::greater{}, &MixIssue::severity);
        return std::move(issues_);
    }

private:
    [[nodiscard]] const std::vector<WindowLevels>& windows(std::size_t output) const {
        return outputs_[output].windows;
    }

    [[nodiscard]] Run whole(std::size_t output) const {
        return {0, std::max<std::size_t>(windows(output).size(), 1) - 1};
    }

    void add(IssueSeverity severity, IssueKind kind, std::size_t output, const Run& run, std::string detail,
             std::vector<IssueEvidence> evidence) {
        issues_.push_back({.severity = severity,
                           .kind = kind,
                           .output = output,
                           .firstWindow = run.first,
                           .lastWindow = run.last,
                           .detail = std::move(detail),
                           .evidence = std::move(evidence)});
    }

    void whines(std::size_t output) {
        const auto& all = windows(output);
        const auto steadyTone = [](const WindowLevels& window) {
            return active(window) && window.tonality >= kWhineTonality && window.crestDb < kBeepMinCrestDb;
        };
        for (const Run& run : runsOf(all, steadyTone, sameTone, kWhineMinWindows)) {
            const double hz =
                runMedian(all, run, [](const WindowLevels& window, std::size_t) { return window.tonalPeakHz; });
            const double tonality =
                runMedian(all, run, [](const WindowLevels& window, std::size_t) { return window.tonality; });
            add(tonality >= kWhineStrongTonality ? IssueSeverity::high : IssueSeverity::medium, IssueKind::whine,
                output, run, std::format("a steady {:.0f} Hz tone over {} windows", hz, run.length()),
                {{"hz", hz}, {"tonality", tonality}, {"windows", static_cast<double>(run.length())}});
        }
    }

    void beeps(std::size_t output) {
        const auto& all = windows(output);
        const auto pitchedHits = [](const WindowLevels& window) {
            return active(window) && window.tonality >= kBeepTonality && window.tonalPeakHz >= kBeepMinHz &&
                   window.crestDb >= kBeepMinCrestDb && window.onsets >= kBeepMinOnsets;
        };
        for (const Run& run : runsOf(all, pitchedHits, sameTone, kBeepMinWindows)) {
            const double hz =
                runMedian(all, run, [](const WindowLevels& window, std::size_t) { return window.tonalPeakHz; });
            const double rate = runMedian(
                all, run, [&](const WindowLevels& window, std::size_t index) { return window.onsets / beats_[index]; });
            add(IssueSeverity::high, IssueKind::beep, output, run,
                std::format("a {:.0f} Hz beep {:.2g} times a beat over {} windows", hz, rate, run.length()),
                {{"hz", hz},
                 {"hitsPerBeat", rate},
                 {"tonality",
                  runMedian(all, run, [](const WindowLevels& window, std::size_t) { return window.tonality; })},
                 {"crestDb",
                  runMedian(all, run, [](const WindowLevels& window, std::size_t) { return window.crestDb; })},
                 {"peakDb", runMax(all, run, &WindowLevels::peakDb)}});
        }
    }

    void clipping(std::size_t output) {
        const auto& all = windows(output);
        const auto clips = [](const WindowLevels& window) { return window.clipped > 0; };
        const auto any = [](const WindowLevels&, const WindowLevels&) { return true; };
        for (const Run& run : runsOf(all, clips, any, 1)) {
            const auto samples = std::ranges::fold_left(
                std::span{all}.subspan(run.first, run.length()) | std::views::transform(&WindowLevels::clipped),
                std::int64_t{0}, std::plus{});
            add(outputs_[output].role == OutputRole::mix ? IssueSeverity::high : IssueSeverity::medium,
                IssueKind::clipping, output, run, std::format("{} samples at or above full scale", samples),
                {{"clippedSamples", static_cast<double>(samples)},
                 {"peakDb", runMax(all, run, &WindowLevels::peakDb)}});
        }
    }

    void dc(std::size_t output) {
        const auto& all = windows(output);
        double weighted = 0;
        double beats = 0;
        for (const auto& [window, length] : std::views::zip(all, beats_)) {
            weighted += window.mean * length;
            beats += length;
        }
        const double offset = beats == 0 ? 0 : weighted / beats;
        if (std::abs(offset) > kDcOffset) {
            add(IssueSeverity::medium, IssueKind::dc, output, whole(output),
                std::format("a DC offset of {:.3f}", offset),
                {{"offset", offset}, {"offsetDb", amplitudeDb(std::abs(offset))}});
        }
    }

    void harsh(std::size_t output) {
        const auto& all = windows(output);
        const std::vector<double> shares =
            all | std::views::filter(active) | std::views::transform(highShare) | std::ranges::to<std::vector>();
        if (shares.size() < kHarshMinActiveWindows) {
            return;
        }
        const double average = std::ranges::fold_left(shares, 0.0, std::plus{}) / static_cast<double>(shares.size());
        const auto harsher = [average](const WindowLevels& window) {
            return active(window) && highShare(window) >= average + kHarshShareJump &&
                   highShare(window) >= average * kHarshShareRatio;
        };
        const auto any = [](const WindowLevels&, const WindowLevels&) { return true; };
        for (const Run& run : runsOf(all, harsher, any, kHarshMinWindows)) {
            const double share =
                runMedian(all, run, [](const WindowLevels& window, std::size_t) { return highShare(window); });
            add(IssueSeverity::low, IssueKind::harsh, output, run,
                std::format("{:.0f}% of the energy above {:g} kHz against {:.0f}% on average", share * 100,
                            kToneBands[kFirstHighBand].low / 1000, average * 100),
                {{"highShare", share}, {"averageHighShare", average}});
        }
    }

    void silent(std::size_t output) {
        constexpr std::array kSeverity{IssueSeverity::medium, IssueSeverity::low, IssueSeverity::high};
        add(kSeverity.at(static_cast<std::size_t>(outputs_[output].role)), IssueKind::silent, output, whole(output),
            std::format("nothing above {:.0f} dBFS over the range", kActiveGateDb), {});
    }

    void imbalance() {
        const auto isTrack = [&](std::size_t output) {
            return outputs_[output].role == OutputRole::track &&
                   std::isfinite(outputs_[output].levels.tone.activeRmsDb);
        };
        const std::vector<std::size_t> tracks = std::views::iota(std::size_t{0}, outputs_.size()) |
                                                std::views::filter(isTrack) | std::ranges::to<std::vector>();
        if (tracks.size() < 2) {
            return;
        }
        const double middle =
            median(tracks |
                   std::views::transform([&](std::size_t output) { return outputs_[output].levels.tone.activeRmsDb; }) |
                   std::ranges::to<std::vector>());
        for (const std::size_t output : tracks) {
            const double level = outputs_[output].levels.tone.activeRmsDb;
            const double offset = level - middle;
            if (offset > kLoudOverMedianDb || offset < kQuietUnderMedianDb) {
                add(IssueSeverity::medium, IssueKind::imbalance, output, whole(output),
                    std::format("{:.1f} dB active RMS, {:+.1f} dB from the median track", level, offset),
                    {{"activeRmsDb", level}, {"medianDb", middle}, {"offsetDb", offset}});
            }
        }
    }

    std::span<const InspectedOutput> outputs_;
    std::span<const double> beats_;
    std::vector<MixIssue> issues_;
};

}  // namespace

std::vector<MixIssue> inspectMix(std::span<const InspectedOutput> outputs, std::span<const double> windowBeats) {
    return Finder(outputs, windowBeats).find();
}

}  // namespace emerick::engine
