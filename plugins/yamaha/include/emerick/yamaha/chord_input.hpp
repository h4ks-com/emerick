#pragma once

#include <QObject>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/yamaha/chord.hpp"

namespace emerick::yamaha {

// While a Chords block's picker is open, the next chord played on the track's MIDI input sets that block: the
// keyboard's Chord Control sysex, or the notes held together on the track's channel once they are let go.
class ChordInput {
public:
    ChordInput(app::Registry& registry, app::ProjectController& controller, app::Nav& nav);

private:
    void follow();
    void listen(const model::Track& track);
    void receive(const std::vector<std::uint8_t>& bytes);
    void set(const Chord& chord);

    app::Registry& registry_;
    app::ProjectController& controller_;
    app::Nav& nav_;
    QObject context_;
    QVariantMap block_;
    int channel_ = 0;
    std::set<int> held_;
    std::vector<int> played_;
    // We destroy the input first, so its MIDI thread never reaches the members above.
    std::unique_ptr<engine::MidiInput> input_;
};

}  // namespace emerick::yamaha
