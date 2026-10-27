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

void write(const fs::path& path, std::string_view text) { std::ofstream(path, std::ios::binary) << text; }

std::string zoneXml(const SyntheticZone& zone, std::size_t number) {
    return std::format(
        "  <keybank number=\"{}\"><noteLimitHi>{}</noteLimitHi><noteLimitLo>{}</noteLimitLo>"
        "<velocityLimitHi>{}</velocityLimitHi><velocityLimitLo>{}</velocityLimitLo><volume>0.000000</volume>"
        "<pan>0.000000</pan><centerNote>{}</centerNote><tuneCoarse>{}</tuneCoarse><tuneFine>{:.6f}</tuneFine>"
        "<tuneLoop>0</tuneLoop><samplingFrequency>{:.6f}</samplingFrequency><fixedPitch>OFF</fixedPitch>"
        "<oneShot>{}</oneShot><sample position=\"MONO\"><waveFile>{}</waveFile><waveType>LINEAR16</waveType>"
        "</sample></keybank>\n",
        number, zone.keyHi, zone.keyLo, zone.velocityHi, zone.velocityLo, zone.rootKey, zone.tuneCoarse, zone.tuneFine,
        zone.sampleRate, zone.oneShot ? "ON" : "OFF", zone.wave);
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
        "</oscillatorBlock></blockComposition></voiceElement>\n"
        " </voiceCommon>\n"
        " <information><voiceName>{}</voiceName><voiceMsb>{}</voiceMsb><voiceLsb>{}</voiceLsb>"
        "<voicePcNum>{}</voicePcNum></information>\n"
        " <waveform number=\"7\">\n{} </waveform>\n"
        "</uvf:voiceData>\n",
        voice.keyShift, voice.name, voice.msb, voice.lsb, voice.program, zones);
}

}  // namespace

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

Bytes compressedBlob() {
    Bytes apcp;
    putText(apcp, "WXCC");
    apcp.resize(36, 0);
    Bytes wxc1{0x08, 0x00, 0x01, 0x01, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01};
    wxc1.resize(wxc1.size() + 54 + 64, 0x5A);
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
