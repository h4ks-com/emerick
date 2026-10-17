#pragma once

#include <expected>
#include <string>
#include <utility>

#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::model {

Ticks clipStart(const Clip& clip);
Ticks clipLength(const Clip& clip);
Ticks clipEnd(const Clip& clip);

Clip moveClip(Clip clip, Ticks start);
// The copy gets a new id.
Clip duplicateClip(Clip clip, Ticks start);

// Each edit returns the edited clip or a message saying why the edit cannot happen. MIDI edits keep only the events
// inside the clip, end a note held across a cut with a note-off at the cut and drop note-offs whose note-on was cut.

// Moves the left edge to start and keeps the end and the material where they were on the timeline.
std::expected<Clip, std::string> trimStart(const Clip& clip, Ticks start, const core::TempoMap& tempo,
                                           double sampleRate);
std::expected<Clip, std::string> setLength(const Clip& clip, Ticks length);
// The right part gets a new id.
std::expected<std::pair<Clip, Clip>, std::string> split(const Clip& clip, Ticks at, const core::TempoMap& tempo,
                                                        double sampleRate);

}  // namespace emerick::model
