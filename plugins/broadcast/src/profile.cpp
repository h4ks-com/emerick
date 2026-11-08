#include "profile.hpp"

#include <QByteArray>
#include <QUrl>
#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <string_view>
#include <utility>

#include "emerick/app/params.hpp"

namespace emerick::broadcast {

namespace {

struct FormatName {
    std::string_view key;
    Format format;
    std::string_view contentType;
};

constexpr std::array kFormats{FormatName{"opus", Format::opus, "audio/ogg"},
                              FormatName{"mp3", Format::mp3, "audio/mpeg"}};
constexpr int kMaxPort = 65535;
constexpr int kMinBitrate = 32;
constexpr int kMaxBitrate = 320;
constexpr int kMaxRetrySeconds = 30;
constexpr int kMaxDoublings = 5;

const FormatName& formatName(Format format) { return *std::ranges::find(kFormats, format, &FormatName::format); }

// A header value with a line break would let the text add headers of its own.
std::string headerText(const app::Json& params, const std::string& key, std::string current) {
    std::string value = app::optionalParam<std::string>(params, key).value_or(std::move(current));
    if (std::ranges::any_of(value, [](char character) { return character == '\r' || character == '\n'; })) {
        throw app::AppError("param " + key + " cannot hold a line break");
    }
    return value;
}

int boundedInt(const app::Json& params, const std::string& key, int current, int minimum, int maximum) {
    const int value = app::optionalParam<int>(params, key).value_or(current);
    if (value < minimum || value > maximum) {
        throw app::AppError(std::format("param {} takes {} to {}, got {}", key, minimum, maximum, value));
    }
    return value;
}

std::string basicAuth(const Profile& profile) {
    return QByteArray::fromStdString(profile.user + ":" + profile.password).toBase64().toStdString();
}

std::string hostHeader(const Profile& profile) { return std::format("{}:{}", profile.host, profile.port); }

std::string percentEncoded(const std::string& text) {
    return QUrl::toPercentEncoding(QString::fromStdString(text)).toStdString();
}

}  // namespace

Profile parseProfile(const app::Json& params, Profile base) {
    Profile profile = std::move(base);
    profile.name = headerText(params, "name", profile.name);
    profile.host = headerText(params, "host", profile.host);
    if (profile.name.empty() || profile.host.empty()) {
        throw app::AppError("a broadcast needs a name and a server host");
    }
    profile.port = boundedInt(params, "port", profile.port, 1, kMaxPort);
    profile.tls = app::optionalParam<bool>(params, "tls").value_or(profile.tls);
    profile.mount = headerText(params, "mount", profile.mount);
    if (!profile.mount.starts_with('/')) {
        profile.mount.insert(0, "/");
    }
    if (profile.mount.contains(' ')) {
        throw app::AppError("param mount cannot hold a space, got " + profile.mount);
    }
    profile.user = headerText(params, "user", profile.user);
    if (profile.user.contains(':')) {
        throw app::AppError("param user cannot hold a colon");
    }
    if (std::string password = app::optionalParam<std::string>(params, "password").value_or(""); !password.empty()) {
        profile.password = std::move(password);
    }
    if (profile.password.empty()) {
        throw app::AppError("a broadcast needs the server's source password");
    }
    if (const auto format = app::optionalParam<std::string>(params, "format")) {
        const auto* const found = std::ranges::find(kFormats, *format, &FormatName::key);
        if (found == kFormats.end()) {
            throw app::AppError("param format takes opus or mp3, got " + *format);
        }
        profile.format = found->format;
    }
    profile.bitrate = boundedInt(params, "bitrate", profile.bitrate, kMinBitrate, kMaxBitrate);
    profile.streamName = headerText(params, "streamName", profile.streamName);
    profile.genre = headerText(params, "genre", profile.genre);
    profile.description = headerText(params, "description", profile.description);
    return profile;
}

app::Json storedProfile(const Profile& profile) {
    app::Json stored = maskedProfile(profile);
    stored["password"] = profile.password;
    return stored;
}

Profile profileFromStore(const app::Json& stored) { return parseProfile(stored, {}); }

app::Json maskedProfile(const Profile& profile) {
    return {{"name", profile.name},
            {"host", profile.host},
            {"port", profile.port},
            {"tls", profile.tls},
            {"mount", profile.mount},
            {"user", profile.user},
            {"password", profile.password.empty() ? "" : "********"},
            {"format", formatName(profile.format).key},
            {"bitrate", profile.bitrate},
            {"streamName", profile.streamName},
            {"genre", profile.genre},
            {"description", profile.description}};
}

std::string contentType(Format format) { return std::string{formatName(format).contentType}; }

std::vector<std::string> encoderArguments(const Profile& profile, double sampleRate, const std::string& title) {
    std::vector<std::string> arguments{"-hide_banner", "-loglevel", "error", "-nostdin",
                                       "-f",           "f32le",     "-ar",   std::format("{}", sampleRate),
                                       "-ac",          "2",         "-i",    "pipe:0"};
    const std::vector<std::string> codec =
        profile.format == Format::opus
            ? std::vector<std::string>{"-c:a", "libopus", "-ar", "48000", "-application", "audio", "-f", "ogg"}
            : std::vector<std::string>{"-c:a", "libmp3lame", "-f", "mp3"};
    std::ranges::copy(codec, std::back_inserter(arguments));
    std::ranges::copy(std::vector<std::string>{"-b:a", std::format("{}k", profile.bitrate), "-flush_packets", "1",
                                               "-metadata", "title=" + title, "pipe:1"},
                      std::back_inserter(arguments));
    return arguments;
}

std::string sourceRequest(const Profile& profile) {
    std::string request = std::format(
        "PUT {} HTTP/1.1\r\nHost: {}\r\nAuthorization: Basic {}\r\nUser-Agent: Emerick\r\nContent-Type: {}\r\n"
        "Ice-Public: 0\r\nIce-Audio-Info: bitrate={}\r\nExpect: 100-continue\r\n",
        profile.mount, hostHeader(profile), basicAuth(profile), contentType(profile.format), profile.bitrate);
    for (const auto& [header, value] :
         {std::pair{"Ice-Name", &profile.streamName}, std::pair{"Ice-Genre", &profile.genre},
          std::pair{"Ice-Description", &profile.description}}) {
        if (!value->empty()) {
            request += std::format("{}: {}\r\n", header, *value);
        }
    }
    return request + "\r\n";
}

std::string metadataRequest(const Profile& profile, const std::string& title) {
    return std::format(
        "GET /admin/metadata?mount={}&mode=updinfo&song={} HTTP/1.0\r\nHost: {}\r\nAuthorization: Basic {}\r\n"
        "User-Agent: Emerick\r\n\r\n",
        percentEncoded(profile.mount), percentEncoded(title), hostHeader(profile), basicAuth(profile));
}

std::chrono::seconds retryDelay(int failures) {
    return std::chrono::seconds{std::min(kMaxRetrySeconds, 1 << std::clamp(failures - 1, 0, kMaxDoublings))};
}

}  // namespace emerick::broadcast
