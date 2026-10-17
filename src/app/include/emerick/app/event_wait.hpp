#pragma once

#include <QCoreApplication>
#include <QEventLoop>
#include <chrono>
#include <functional>
#include <thread>

namespace emerick::app {

// We skip user input and sockets, so a click or another MCP request never runs inside the wait.
inline bool processEventsUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}

}  // namespace emerick::app
