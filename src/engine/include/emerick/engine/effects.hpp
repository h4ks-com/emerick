#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace emerick::engine {

// The built-in insert effects. The limiter delays its track by its lookahead; the other effects add no delay.
enum class EffectType : std::uint8_t {
    eq,
    compressor,
    delay,
    reverb,
    limiter,
    gate,
    filter,
    saturation,
    chorus,
    utility
};

enum class ParamKind : std::uint8_t { number, toggle, choice };

// The tables leave trailing fields out, so every field has a default; this check calls those defaults redundant.
// NOLINTBEGIN(readability-redundant-member-init)
struct EffectParam {
    std::string_view id;
    std::string_view title;
    std::string_view unit;
    float minimum = 0.0F;
    float maximum = 1.0F;
    float defaultValue = 0.0F;
    ParamKind kind = ParamKind::number;
    // A logarithmic param, such as a frequency, moves by ratios when dragged.
    bool logarithmic = false;
    std::span<const std::string_view> choices = {};
};
// NOLINTEND(readability-redundant-member-init)

struct EffectSpec {
    EffectType type;
    std::string_view id;
    std::string_view title;
    std::span<const EffectParam> params;
    // The effect reports the gain it takes away, which the insert editor meters.
    bool reportsReduction = false;
};

inline constexpr std::size_t kMaxEffectParams = 28;
using EffectParams = std::array<float, kMaxEffectParams>;

// The Channel EQ has seven bands of four params each, in this order, so band b's params start at 4 * b.
enum class EqBand : std::uint8_t { lowCut, lowShelf, peak1, peak2, peak3, highShelf, highCut };
inline constexpr std::size_t kEqBands = 7;
inline constexpr std::size_t kEqBandParams = 4;
enum class EqBandParam : std::uint8_t { on, frequency, gain, q };
// A cut band's third param is its slope, an index into kCutSlopes.
inline constexpr std::array<std::string_view, 4> kCutSlopes{"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"};

enum class CompressorParam : std::uint8_t { threshold, ratio, attack, release, knee, makeup, autoMakeup };
enum class DelayParam : std::uint8_t { time, sync, note, feedback, mix, highCut };
enum class ReverbParam : std::uint8_t { size, decay, damping, preDelay, mix };
enum class LimiterParam : std::uint8_t { ceiling, release, gain };
enum class GateParam : std::uint8_t { threshold, range, attack, hold, release };
enum class FilterParam : std::uint8_t { mode, cutoff, resonance, slope };
enum class SaturationParam : std::uint8_t { drive, character, output, mix };
enum class ChorusParam : std::uint8_t { rate, depth, delay, feedback, mix };
enum class UtilityParam : std::uint8_t { gain, width, mono, invertLeft, invertRight, balance };

enum class FilterMode : std::uint8_t { lowPass, highPass, bandPass };
inline constexpr std::array<std::string_view, 3> kFilterModes{"Low pass", "High pass", "Band pass"};
inline constexpr std::array<std::string_view, 2> kFilterSlopes{"12 dB/oct", "24 dB/oct"};
enum class SaturationCharacter : std::uint8_t { softClip, tape, tube };
inline constexpr std::array<std::string_view, 3> kSaturationCharacters{"Soft clip", "Tape", "Tube"};
inline constexpr float kMaxChorusDelayMs = 30.0F;
inline constexpr float kMaxChorusDepthMs = 5.0F;

// A synced delay's note values and their lengths in quarter notes.
inline constexpr std::array<std::string_view, 13> kNoteNames{"1/1",  "1/2",  "1/2.", "1/4",   "1/4.",  "1/4t", "1/8",
                                                             "1/8.", "1/8t", "1/16", "1/16.", "1/16t", "1/32"};
inline constexpr std::array<float, kNoteNames.size()> kNoteBeats{
    4.0F, 2.0F, 3.0F, 1.0F, 1.5F, 2.0F / 3.0F, 0.5F, 0.75F, 1.0F / 3.0F, 0.25F, 0.375F, 1.0F / 6.0F, 0.125F};
inline constexpr float kMaxDelayMs = 2000.0F;
inline constexpr float kMaxPreDelayMs = 200.0F;

inline constexpr float kMinFrequency = 20.0F;
inline constexpr float kMaxFrequency = 20000.0F;

// NOLINTBEGIN(readability-magic-numbers): the tables are the place these numbers are named.
inline constexpr std::array kEqParams{
    EffectParam{"lowCutOn", "Low cut", "", 0, 1, 0, ParamKind::toggle},
    EffectParam{"lowCutFreq", "Low cut frequency", "Hz", kMinFrequency, kMaxFrequency, 30, ParamKind::number, true},
    EffectParam{"lowCutSlope", "Low cut slope", "", 0, 3, 0, ParamKind::choice, false, kCutSlopes},
    EffectParam{"lowCutQ", "Low cut Q", "", 0.1F, 10, 0.71F, ParamKind::number, true},
    EffectParam{"lowShelfOn", "Low shelf", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"lowShelfFreq", "Low shelf frequency", "Hz", kMinFrequency, kMaxFrequency, 80, ParamKind::number, true},
    EffectParam{"lowShelfGain", "Low shelf gain", "dB", -24, 24, 0},
    EffectParam{"lowShelfQ", "Low shelf Q", "", 0.1F, 10, 0.71F, ParamKind::number, true},
    EffectParam{"peak1On", "Low mid", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"peak1Freq", "Low mid frequency", "Hz", kMinFrequency, kMaxFrequency, 250, ParamKind::number, true},
    EffectParam{"peak1Gain", "Low mid gain", "dB", -24, 24, 0},
    EffectParam{"peak1Q", "Low mid Q", "", 0.1F, 10, 1, ParamKind::number, true},
    EffectParam{"peak2On", "Mid", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"peak2Freq", "Mid frequency", "Hz", kMinFrequency, kMaxFrequency, 1000, ParamKind::number, true},
    EffectParam{"peak2Gain", "Mid gain", "dB", -24, 24, 0},
    EffectParam{"peak2Q", "Mid Q", "", 0.1F, 10, 1, ParamKind::number, true},
    EffectParam{"peak3On", "High mid", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"peak3Freq", "High mid frequency", "Hz", kMinFrequency, kMaxFrequency, 4000, ParamKind::number, true},
    EffectParam{"peak3Gain", "High mid gain", "dB", -24, 24, 0},
    EffectParam{"peak3Q", "High mid Q", "", 0.1F, 10, 1, ParamKind::number, true},
    EffectParam{"highShelfOn", "High shelf", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"highShelfFreq", "High shelf frequency", "Hz", kMinFrequency, kMaxFrequency, 10000, ParamKind::number,
                true},
    EffectParam{"highShelfGain", "High shelf gain", "dB", -24, 24, 0},
    EffectParam{"highShelfQ", "High shelf Q", "", 0.1F, 10, 0.71F, ParamKind::number, true},
    EffectParam{"highCutOn", "High cut", "", 0, 1, 0, ParamKind::toggle},
    EffectParam{"highCutFreq", "High cut frequency", "Hz", kMinFrequency, kMaxFrequency, 18000, ParamKind::number,
                true},
    EffectParam{"highCutSlope", "High cut slope", "", 0, 3, 0, ParamKind::choice, false, kCutSlopes},
    EffectParam{"highCutQ", "High cut Q", "", 0.1F, 10, 0.71F, ParamKind::number, true},
};
static_assert(kEqParams.size() == kEqBands * kEqBandParams);

inline constexpr std::array kCompressorParams{
    EffectParam{"threshold", "Threshold", "dB", -60, 0, -18},
    EffectParam{"ratio", "Ratio", ":1", 1, 20, 4, ParamKind::number, true},
    EffectParam{"attack", "Attack", "ms", 0.1F, 200, 10, ParamKind::number, true},
    EffectParam{"release", "Release", "ms", 5, 2000, 150, ParamKind::number, true},
    EffectParam{"knee", "Knee", "dB", 0, 24, 6},
    EffectParam{"makeup", "Make up", "dB", 0, 24, 0},
    EffectParam{"autoMakeup", "Auto gain", "", 0, 1, 0, ParamKind::toggle},
};

inline constexpr std::array kDelayParams{
    EffectParam{"time", "Time", "ms", 1, kMaxDelayMs, 375, ParamKind::number, true},
    EffectParam{"sync", "Sync", "", 0, 1, 1, ParamKind::toggle},
    EffectParam{"note", "Note", "", 0, kNoteNames.size() - 1, 6, ParamKind::choice, false, kNoteNames},
    EffectParam{"feedback", "Feedback", "%", 0, 95, 30},
    EffectParam{"mix", "Mix", "%", 0, 100, 25},
    EffectParam{"highCut", "High cut", "Hz", 500, kMaxFrequency, 8000, ParamKind::number, true},
};

inline constexpr std::array kReverbParams{
    EffectParam{"size", "Size", "%", 0, 100, 50},
    EffectParam{"decay", "Decay", "s", 0.2F, 10, 2, ParamKind::number, true},
    EffectParam{"damping", "Damping", "%", 0, 100, 50},
    EffectParam{"preDelay", "Pre-delay", "ms", 0, kMaxPreDelayMs, 10},
    EffectParam{"mix", "Mix", "%", 0, 100, 20},
};

inline constexpr std::array kLimiterParams{
    EffectParam{"ceiling", "Ceiling", "dBTP", -12, 0, -1},
    EffectParam{"release", "Release", "ms", 1, 1000, 50, ParamKind::number, true},
    EffectParam{"gain", "Input gain", "dB", -12, 24, 0},
};

inline constexpr std::array kGateParams{
    EffectParam{"threshold", "Threshold", "dB", -80, 0, -40},
    EffectParam{"range", "Range", "dB", 0, 80, 80},
    EffectParam{"attack", "Attack", "ms", 0.01F, 50, 1, ParamKind::number, true},
    EffectParam{"hold", "Hold", "ms", 0, 500, 20},
    EffectParam{"release", "Release", "ms", 5, 2000, 100, ParamKind::number, true},
};

inline constexpr std::array kFilterParams{
    EffectParam{"mode", "Mode", "", 0, kFilterModes.size() - 1, 0, ParamKind::choice, false, kFilterModes},
    EffectParam{"cutoff", "Cutoff", "Hz", kMinFrequency, kMaxFrequency, 2000, ParamKind::number, true},
    EffectParam{"resonance", "Resonance", "%", 0, 100, 0},
    EffectParam{"slope", "Slope", "", 0, kFilterSlopes.size() - 1, 0, ParamKind::choice, false, kFilterSlopes},
};

inline constexpr std::array kSaturationParams{
    EffectParam{"drive", "Drive", "dB", 0, 36, 6},
    EffectParam{"character", "Character", "", 0, kSaturationCharacters.size() - 1, 0, ParamKind::choice, false,
                kSaturationCharacters},
    EffectParam{"output", "Output", "dB", -24, 24, 0},
    EffectParam{"mix", "Mix", "%", 0, 100, 100},
};

inline constexpr std::array kChorusParams{
    EffectParam{"rate", "Rate", "Hz", 0.05F, 5, 0.8F, ParamKind::number, true},
    EffectParam{"depth", "Depth", "%", 0, 100, 50},
    EffectParam{"delay", "Delay", "ms", 5, kMaxChorusDelayMs, 12},
    EffectParam{"feedback", "Feedback", "%", 0, 90, 0},
    EffectParam{"mix", "Mix", "%", 0, 100, 50},
};

inline constexpr std::array kUtilityParams{
    EffectParam{"gain", "Gain", "dB", -60, 24, 0},
    EffectParam{"width", "Width", "%", 0, 200, 100},
    EffectParam{"mono", "Mono", "", 0, 1, 0, ParamKind::toggle},
    EffectParam{"invertLeft", "Invert left", "", 0, 1, 0, ParamKind::toggle},
    EffectParam{"invertRight", "Invert right", "", 0, 1, 0, ParamKind::toggle},
    EffectParam{"balance", "Balance", "%", -100, 100, 0},
};
// NOLINTEND(readability-magic-numbers)

// We index the table by EffectType, so its rows follow the enum's order.
inline constexpr std::array kEffects{
    EffectSpec{EffectType::eq, "eq", "Channel EQ", kEqParams},
    EffectSpec{EffectType::compressor, "compressor", "Compressor", kCompressorParams, true},
    EffectSpec{EffectType::delay, "delay", "Delay", kDelayParams},
    EffectSpec{EffectType::reverb, "reverb", "Reverb", kReverbParams},
    EffectSpec{EffectType::limiter, "limiter", "Limiter", kLimiterParams, true},
    EffectSpec{EffectType::gate, "gate", "Gate", kGateParams},
    EffectSpec{EffectType::filter, "filter", "Filter", kFilterParams},
    EffectSpec{EffectType::saturation, "saturation", "Saturation", kSaturationParams},
    EffectSpec{EffectType::chorus, "chorus", "Chorus", kChorusParams},
    EffectSpec{EffectType::utility, "utility", "Utility", kUtilityParams},
};

const EffectSpec& effectSpec(EffectType type);
const EffectSpec* findEffect(std::string_view id);
std::optional<std::size_t> paramIndex(const EffectSpec& spec, std::string_view id);
EffectParams defaultParams(const EffectSpec& spec);

// The Channel EQ's response in dB at frequency, the sum of its bands as the engine plays them.
double eqResponseDb(const EffectParams& params, double frequency, double sampleRate);

}  // namespace emerick::engine
