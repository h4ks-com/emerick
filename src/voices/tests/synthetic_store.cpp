#include "synthetic_store.hpp"

#include <bit>
#include <cmath>
#include <format>
#include <fstream>
#include <numbers>
#include <random>
#include <ranges>
#include <string_view>

namespace emerick::voices::test {

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void put32(Bytes& bytes, std::uint32_t value) {
    put16(bytes, value >> 16U);
    put16(bytes, value & 0xFFFFU);
}

void putText(Bytes& bytes, std::string_view text) { bytes.insert(bytes.end(), text.begin(), text.end()); }

void putChunk(Bytes& form, std::string_view tag, const Bytes& body) {
    putText(form, tag);
    put32(form, static_cast<std::uint32_t>(body.size()));
    form.insert(form.end(), body.begin(), body.end());
    if (body.size() % 2 != 0) {
        form.push_back(0);
    }
}

// An IEEE 754 80-bit extended number, as AIFF's COMM chunk stores the sample rate.
void putExtended(Bytes& bytes, double value) {
    const auto whole = static_cast<std::uint64_t>(value);
    const int exponent = std::bit_width(whole) - 1;
    put16(bytes, static_cast<std::uint32_t>(16383 + exponent));
    const std::uint64_t mantissa = whole << static_cast<unsigned>(63 - exponent);
    put32(bytes, static_cast<std::uint32_t>(mantissa >> 32U));
    put32(bytes, static_cast<std::uint32_t>(mantissa & 0xFFFFFFFFU));
}

Bytes form(const std::vector<std::pair<std::string_view, Bytes>>& chunks) {
    Bytes body;
    putText(body, "YTWV");
    for (const auto& [tag, chunk] : chunks) {
        putChunk(body, tag, chunk);
    }
    Bytes blob;
    putText(blob, "FORM");
    put32(blob, static_cast<std::uint32_t>(body.size()));
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

void putMarker(Bytes& mark, std::uint32_t id, std::uint32_t position, std::string_view name) {
    put16(mark, id);
    put32(mark, position);
    mark.push_back(static_cast<std::uint8_t>(name.size()));
    putText(mark, name);
    if ((name.size() + 1) % 2 != 0) {
        mark.push_back(0);
    }
}

std::array<std::uint16_t, 3> packed(const WxcParams& params) {
    const auto [a1, a2, a3] = params.coefficients;
    const auto scale = static_cast<unsigned>(params.scale);
    return {
        static_cast<std::uint16_t>((static_cast<unsigned>(a1) << 3U) | (scale >> 3U)),
        static_cast<std::uint16_t>((static_cast<unsigned>(a2) << 3U) | (scale & 7U)),
        static_cast<std::uint16_t>(((static_cast<unsigned>(a3) & 0xFFFU) << 3U) | static_cast<unsigned>(params.mode))};
}

// The scramble only mixes the low 12 bits into the top four, so the same function undoes it.
std::uint32_t scrambled(unsigned word) { return ((word ^ (word << 6U) ^ (word << 12U)) & 0xF000U) | (word & 0x0FFFU); }

void write(const fs::path& path, std::string_view text) { std::ofstream(path, std::ios::binary) << text; }

std::string zoneXml(const SyntheticZone& zone, std::size_t number) {
    return std::format(
        "  <keybank number=\"{}\"><noteLimitHi>{}</noteLimitHi><noteLimitLo>{}</noteLimitLo>"
        "<velocityLimitHi>{}</velocityLimitHi><velocityLimitLo>{}</velocityLimitLo><volume>0.000000</volume>"
        "<pan>0.000000</pan><centerNote>{}</centerNote><tuneCoarse>{}</tuneCoarse><tuneFine>{:.6f}</tuneFine>"
        "<tuneLoop>0</tuneLoop><samplingFrequency>{:.6f}</samplingFrequency><fixedPitch>OFF</fixedPitch>"
        "<oneShot>{}</oneShot><sample position=\"MONO\"><waveFile>{}</waveFile><waveType>{}</waveType>"
        "</sample></keybank>\n",
        number, zone.keyHi, zone.keyLo, zone.velocityHi, zone.velocityLo, zone.rootKey, zone.tuneCoarse, zone.tuneFine,
        zone.sampleRate, zone.oneShot ? "ON" : "OFF", zone.wave, zone.waveType);
}

std::string voiceXml(const SyntheticVoice& voice) {
    std::string zones;
    for (std::size_t number = 0; number < voice.zones.size(); ++number) {
        zones += zoneXml(voice.zones[number], number);
    }
    return std::format(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<uvf:voiceData version=\"1.5\" xmlns:uvf=\"http://www.yamaha.com/uvf\">\n"
        " <voiceType>EKB_LEGACY</voiceType>\n"
        " <voiceCommon><volume>0.000000</volume>\n"
        "  <voiceElement number=\"0\"><volume>0.000000</volume><pan>0.000000</pan><noteShift>{}</noteShift>"
        "<noteLimitHi>G8</noteLimitHi><noteLimitLo>C-2</noteLimitLo><velocityLimitHi>127</velocityLimitHi>"
        "<velocityLimitLo>1</velocityLimitLo><blockComposition><oscillatorBlock><waveformNum>7</waveformNum>"
        "</oscillatorBlock>{}</blockComposition><drumSetup>{}</drumSetup></voiceElement>\n"
        " </voiceCommon>\n"
        " <information><voiceName>{}</voiceName><voiceMsb>{}</voiceMsb><voiceLsb>{}</voiceLsb>"
        "<voicePcNum>{}</voicePcNum></information>\n"
        " <waveform number=\"7\">\n{} </waveform>\n"
        "</uvf:voiceData>\n",
        voice.keyShift, voice.blocks, voice.drumSetup, voice.name, voice.msb, voice.lsb, voice.program, zones);
}

std::string curveXml(std::string_view tag, const std::vector<int>& curve) {
    if (curve.empty()) {
        return std::format("<{0} number=\"0\"><name></name><presetTable>-1</presetTable></{0}>", tag);
    }
    std::string steps;
    for (const int step : curve) {
        steps += std::format("{} ", step);
    }
    return std::format("<{0} number=\"0\"><name></name><table>{1}</table></{0}>", tag, steps);
}

}  // namespace

std::string legacyFilterBlock(const SyntheticLegacyFilter& filter) {
    const auto& [initial, first, second, third, sustain, release] = filter.levels;
    const auto& [firstRate, secondRate, thirdRate, sustainRate, releaseRate] = filter.rates;
    return std::format(
        "<filterBlock xsi:type=\"uvf:FILTER_BLOCK_EKB_LEGACY\"><dcfFilterType>{}</dcfFilterType>"
        "<dcfResonance>0.000000</dcfResonance><dcfCutoff>{:.6f}</dcfCutoff><EGDepth>{}</EGDepth>"
        "<cegHoldTime>{}</cegHoldTime><ceg1stRate>{:.6f}</ceg1stRate><ceg2ndRate>{:.6f}</ceg2ndRate>"
        "<ceg3rdRate>{:.6f}</ceg3rdRate><cegReleaseRate>{:.6f}</cegReleaseRate>"
        "<cegSustainRate>{:.6f}</cegSustainRate><cegInitialLevel>{:.6f}</cegInitialLevel>"
        "<ceg1stLevel>{:.6f}</ceg1stLevel><ceg2ndLevel>{:.6f}</ceg2ndLevel><ceg3rdLevel>{:.6f}</ceg3rdLevel>"
        "<cegReleaseLevel>{:.6f}</cegReleaseLevel><cegSustainLevel>{:.6f}</cegSustainLevel>"
        "<dcfCegEnable>ON</dcfCegEnable>{}{}</filterBlock>",
        filter.type, filter.cutoff, filter.egDepth, filter.holdTime, firstRate, secondRate, thirdRate, releaseRate,
        sustainRate, initial, first, second, third, release, sustain,
        curveXml("dcfCutoffScalingTable", filter.keyCurve), curveXml("dcfVelToCutoffTable", filter.velocityCurve));
}

std::string touchCurveBlock(const std::vector<int>& curve) {
    return std::format("<amplitudeBlock xsi:type=\"uvf:AMPLITUDE_BLOCK_EKB_LEGACY\">{}</amplitudeBlock>",
                       curveXml("touchCurveTable", curve));
}

std::vector<int> ramp(int first, int last) {
    return std::views::iota(0, 128) | std::views::transform([&](int index) {
               return first + static_cast<int>(std::lround((last - first) * index / 127.0));
           }) |
           std::ranges::to<std::vector>();
}

std::vector<std::int16_t> sine(double frequency, double sampleRate, std::size_t frames) {
    constexpr double kAmplitude = 16000.0;
    return std::views::iota(std::size_t{0}, frames) | std::views::transform([&](std::size_t frame) {
               const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / sampleRate;
               return static_cast<std::int16_t>(std::lround(kAmplitude * std::sin(phase)));
           }) |
           std::ranges::to<std::vector>();
}

Bytes linearBlob(const std::vector<std::int16_t>& samples, double sampleRate,
                 std::optional<std::pair<std::uint32_t, std::uint32_t>> loop) {
    Bytes comm;
    put16(comm, 1);
    put32(comm, static_cast<std::uint32_t>(samples.size()));
    put16(comm, 16);
    putExtended(comm, sampleRate);
    Bytes ssnd;
    put32(ssnd, 0);
    put32(ssnd, 0);
    for (const std::int16_t sample : samples) {
        put16(ssnd, static_cast<std::uint16_t>(sample));
    }
    Bytes mark;
    put16(mark, loop ? 2 : 0);
    if (loop) {
        putMarker(mark, 1, loop->first, "beg loop");
        putMarker(mark, 2, loop->second, "end loop");
    }
    Bytes inst(8, 0);
    for (const std::uint32_t field : {loop ? 1U : 0U, 1U, 2U, 0U, 0U, 0U}) {
        put16(inst, field);
    }
    return form({{"COMM", comm}, {"MARK", mark}, {"INST", inst}, {"SSND", ssnd}});
}

Bytes wxc1Blob(double sampleRate, const WxcParams& first, const std::vector<WxcFrame>& frames,
               std::pair<std::uint32_t, std::uint32_t> loop) {
    Bytes apcp;
    putText(apcp, "WXCC");
    put32(apcp, 0);
    const auto rate = std::bit_cast<std::uint64_t>(sampleRate);
    put32(apcp, static_cast<std::uint32_t>(rate >> 32U));
    put32(apcp, static_cast<std::uint32_t>(rate & 0xFFFFFFFFU));
    apcp.resize(36, 0);
    Bytes wxc1(6, 0);
    put32(wxc1, loop.first);
    put32(wxc1, loop.second);
    wxc1.resize(22, 0);
    for (const std::uint16_t field : packed(first)) {
        put16(wxc1, field);
    }
    wxc1.resize(68, 0);
    for (const WxcFrame& frame : frames) {
        const std::array<std::uint16_t, 3> next = packed(frame.next);
        for (std::size_t word = 0; word < frame.codes.size(); ++word) {
            unsigned value = frame.codes.at(word) & 0x8FFFU;
            if (word < 13) {
                for (std::size_t field = 0; field < 3; ++field) {
                    value |= ((next.at(field) >> (15 - word)) & 1U) << (14 - field);
                }
            } else {
                value |= (next.at(std::array<std::size_t, 3>{2, 0, 1}.at(word - 13)) & 7U) << 12U;
            }
            put16(wxc1, scrambled(value));
        }
    }
    return form({{"APCp", apcp}, {"WXC1", wxc1}});
}

SyntheticStore::SyntheticStore() {
    std::random_device random;
    folder_ = fs::temp_directory_path() / std::format("emerick-voices-{:08x}{:08x}", random(), random());
    fs::create_directories(folder_ / "{waves}");
}

SyntheticStore::~SyntheticStore() {
    std::error_code error;
    fs::remove_all(folder_, error);
}

std::string SyntheticStore::addWave(const Bytes& blob) {
    const std::string name = std::format("{:040x}", ++waves_);
    std::ofstream file(folder_ / "{waves}" / name, std::ios::binary);
    for (const std::uint8_t byte : blob) {
        file.put(static_cast<char>(byte));
    }
    return name;
}

void SyntheticStore::addPack(const std::string& name, const std::vector<SyntheticVoice>& voices) {
    const std::string pack = std::format("{{pack-{}}}", ++packs_);
    fs::create_directories(folder_ / pack);
    write(folder_ / pack / ".index", std::format("{}\t{}\t\t\t\t\t\t\n", pack, name));
    for (const SyntheticVoice& voice : voices) {
        write(folder_ / pack / std::format("{{item-{}}}", ++items_), voiceXml(voice));
    }
}

}  // namespace emerick::voices::test
