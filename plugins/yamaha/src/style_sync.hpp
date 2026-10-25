#pragma once

#include <QObject>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/project_controller.hpp"

namespace emerick::yamaha {

// Listens to the keyboard to tell whether its Style runs and which Main it reports, and while Emerick plays the Style
// track it shows the section, the bar of the pattern and the chord in the transport, with a warning when the
// keyboard does not follow.
class StyleSync : public QObject {
    Q_OBJECT

public:
    explicit StyleSync(app::ProjectController& controller);

    [[nodiscard]] bool styleRunning() const;

private:
    using Clock = std::chrono::steady_clock;

    void follow();
    void receive(const std::vector<std::uint8_t>& bytes);
    void report();
    void show(app::SyncStatus status);

    app::ProjectController& controller_;
    std::string port_;
    Clock::time_point lastStyleNote_;
    Clock::time_point playStarted_;
    std::optional<int> keyboardMain_;
    app::SyncStatus shown_;
    // We destroy the input first, so its MIDI thread never reaches the members above.
    std::unique_ptr<engine::MidiInput> input_;
};

}  // namespace emerick::yamaha
