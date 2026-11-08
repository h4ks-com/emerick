#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "emerick/app/variant.hpp"

namespace emerick::broadcast {

enum class Format : std::uint8_t { opus, mp3 };

// One Icecast server a broadcast streams the master to.
struct Profile {
    std::string name;
    std::string host;
    int port = 8000;
    bool tls = false;
    std::string mount = "/stream";
    std::string user = "source";
    std::string password;
    Format format = Format::opus;
    int bitrate = 128;
    std::string streamName;
    std::string genre;
    std::string description;
};

// Reads a profile from action params over base, so an edit names only what changes and an empty password keeps the
// stored one; throws AppError naming the param it cannot use.
Profile parseProfile(const app::Json& params, Profile base);
Profile profileFromStore(const app::Json& stored);
app::Json storedProfile(const Profile& profile);
// The profile as results show it, with the password masked.
app::Json maskedProfile(const Profile& profile);

[[nodiscard]] std::string contentType(Format format);
// The ffmpeg arguments that encode interleaved stereo float PCM at sampleRate from stdin to stdout. They hold no
// credentials, so the process list never shows them.
std::vector<std::string> encoderArguments(const Profile& profile, double sampleRate, const std::string& title);
// The PUT request that opens the mount, with the credentials and the Ice headers.
std::string sourceRequest(const Profile& profile);
std::string metadataRequest(const Profile& profile, const std::string& title);
std::chrono::seconds retryDelay(int failures);

}  // namespace emerick::broadcast
