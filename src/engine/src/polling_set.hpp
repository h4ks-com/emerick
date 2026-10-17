#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace emerick::engine {

// We hold members weakly, so each member's owners decide how long its worker thread polls it.
template <class T>
class PollingSet {
public:
    explicit PollingSet(std::chrono::microseconds period)
        : thread_([this, period](const std::stop_token& stop) {
              while (!stop.stop_requested()) {
                  pollAll();
                  std::this_thread::sleep_for(period);
              }
          }) {}

    void add(const std::shared_ptr<T>& member) {
        const std::scoped_lock lock(mutex_);
        members_.push_back(member);
    }

    void pollAll() {
        const std::scoped_lock lock(mutex_);
        std::erase_if(members_, [](const std::weak_ptr<T>& member) {
            const std::shared_ptr<T> alive = member.lock();
            if (alive) {
                alive->poll();
            }
            return !alive;
        });
    }

private:
    std::mutex mutex_;
    std::vector<std::weak_ptr<T>> members_;
    std::jthread thread_;
};

}  // namespace emerick::engine
