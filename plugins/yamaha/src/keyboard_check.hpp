#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/project_controller.hpp"

namespace emerick::yamaha {

// Listens to the keyboard while its Style plays and names the keyboard setting that makes it send each missing part,
// such as its Rhythm parts or its chords, and the audio buffer size to monitor through.
class KeyboardCheck : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY hintsChanged)

public:
    explicit KeyboardCheck(app::ProjectController& controller);

    void listen(const std::string& port);
    [[nodiscard]] std::vector<std::string> hints() const;
    // Every hint, one per line.
    [[nodiscard]] QString message() const;

signals:
    void hintsChanged();

private:
    void receive(const std::vector<std::uint8_t>& bytes);
    void judge();

    app::ProjectController& controller_;
    std::set<int> channels_;
    bool heardChords_ = false;
    int styleNotes_ = 0;
    std::vector<std::string> keyboardHints_;
    // We destroy the input first, so its MIDI thread never reaches the members above.
    std::unique_ptr<engine::MidiInput> input_;
};

}  // namespace emerick::yamaha
