#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace emerick::voices::test {

struct SyntheticZone {
    std::string keyLo = "C-2";
    std::string keyHi = "G8";
    int velocityLo = 1;
    int velocityHi = 127;
    std::string rootKey = "A3";
    int tuneCoarse = 0;
    double tuneFine = 0.0;
    double sampleRate = 44100.0;
    bool oneShot = false;
    std::string wave;
};

struct SyntheticVoice {
    std::string name;
    int msb = 0;
    int lsb = 0;
    int program = 0;
    int keyShift = 0;
    std::vector<SyntheticZone> zones;
};

std::vector<std::int16_t> sine(double frequency, double sampleRate, std::size_t frames);
// A FORM YTWV blob of 16-bit linear audio, with a sustain loop when one is given.
std::vector<std::uint8_t> linearBlob(const std::vector<std::int16_t>& samples, double sampleRate,
                                     std::optional<std::pair<std::uint32_t, std::uint32_t>> loop = std::nullopt);
// A FORM YTWV blob whose audio is WXC1 compressed, which the instrument cannot decode.
std::vector<std::uint8_t> compressedBlob();

// An Expansion Manager store in a fresh temporary folder, deleted with the object.
class SyntheticStore {
public:
    SyntheticStore();
    ~SyntheticStore();
    SyntheticStore(const SyntheticStore&) = delete;
    SyntheticStore& operator=(const SyntheticStore&) = delete;
    SyntheticStore(SyntheticStore&&) = delete;
    SyntheticStore& operator=(SyntheticStore&&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }
    // Writes the blob into the first pack folder and returns its 40 hex digit name.
    std::string addWave(const std::vector<std::uint8_t>& blob);
    void addPack(const std::string& name, const std::vector<SyntheticVoice>& voices);

private:
    std::filesystem::path folder_;
    int waves_ = 0;
    int packs_ = 0;
    int items_ = 0;
};

}  // namespace emerick::voices::test
