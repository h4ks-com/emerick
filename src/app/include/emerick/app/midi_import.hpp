#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

struct MidiPart {
    std::string name;
    model::MidiClip clip;
};

// Reads a type 0 or 1 MIDI file into one clip per track and channel that plays notes, starting at tick 0 and scaled to
// the project's ticks per quarter. We place events by beats and ignore the file's tempo map. A part takes the track
// name, or fallbackName, plus its channel when its track plays on several. Each clip runs to the end of the bar of its
// last event. Throws AppError when the file cannot be read.
std::vector<MidiPart> readMidiParts(const std::filesystem::path& file, const std::string& fallbackName,
                                    core::Ticks ticksPerBar);

}  // namespace emerick::app
