#pragma once

#include <QThread>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "emerick/engine/signal_tap.hpp"
#include "profile.hpp"

namespace emerick::broadcast {

// Error means the server refused the source, so we stop retrying.
enum class State : std::uint8_t { idle, connecting, live, retrying, error };

struct Status {
    State state = State::idle;
    std::optional<std::chrono::steady_clock::time_point> liveSince;
    std::int64_t bytesSent = 0;
    std::int64_t droppedFrames = 0;
    int failures = 0;
    std::chrono::seconds retryIn{0};
    std::string lastError;
};

struct SharedStatus {
    mutable std::mutex mutex;
    Status status;
    [[nodiscard]] Status read() const {
        const std::scoped_lock lock{mutex};
        return status;
    }
};

class Worker;

// Streams one profile: a thread of its own reads the tap, feeds ffmpeg on stdin, and sends what ffmpeg encodes to the
// server, retrying with backoff until this is destroyed.
class Broadcaster {
public:
    Broadcaster(Profile profile, std::filesystem::path ffmpeg, double sampleRate,
                std::shared_ptr<engine::SignalTap> tap, std::string title);
    ~Broadcaster();
    Broadcaster(const Broadcaster&) = delete;
    Broadcaster& operator=(const Broadcaster&) = delete;
    Broadcaster(Broadcaster&&) = delete;
    Broadcaster& operator=(Broadcaster&&) = delete;

    [[nodiscard]] Status status() const { return status_->read(); }
    [[nodiscard]] const std::shared_ptr<engine::SignalTap>& tap() const { return tap_; }
    [[nodiscard]] double sampleRate() const { return sampleRate_; }
    void setTitle(const std::string& title);

private:
    double sampleRate_;
    std::shared_ptr<engine::SignalTap> tap_;
    std::shared_ptr<SharedStatus> status_;
    QThread thread_;
    std::unique_ptr<Worker> worker_;
};

}  // namespace emerick::broadcast
