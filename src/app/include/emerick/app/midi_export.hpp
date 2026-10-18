#pragma once

#include <filesystem>
#include <string>

#include "emerick/app/registry.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// Writes a type 1 MIDI file at 960 PPQ. The first track holds the title, tempo, meter and the markers of every
// session track's type; then comes one named track per MIDI track of the session with the MIDI it plays, on the
// track's output channel when it has one. Throws AppError when the file cannot be written.
void exportMidi(const engine::Session& session, const model::Document& document, const Registry& registry,
                const std::string& title, const std::filesystem::path& file);

}  // namespace emerick::app
