#pragma once

#include <atomic>
#include <thread>

namespace emerick::engine {

// Takes a plugin's audio guard for the main thread, waiting out the block the audio thread may be processing. The audio
// thread only tries the guard and skips its block while the main thread holds it.
class AudioGuard {
public:
    explicit AudioGuard(std::atomic<bool>& guard) : guard_(guard) {
        while (guard_.exchange(true, std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    ~AudioGuard() { guard_.store(false, std::memory_order_release); }
    AudioGuard(const AudioGuard&) = delete;
    AudioGuard& operator=(const AudioGuard&) = delete;
    AudioGuard(AudioGuard&&) = delete;
    AudioGuard& operator=(AudioGuard&&) = delete;

private:
    std::atomic<bool>& guard_;
};

}  // namespace emerick::engine
