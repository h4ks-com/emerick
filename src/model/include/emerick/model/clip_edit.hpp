#pragma once

#include <expected>
#include <string>
#include <utility>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::model {

Ticks clipStart(const Clip& clip);
Ticks clipLength(const Clip& clip);
// Where the clip ends on the timeline, after its repeats when it loops.
Ticks clipEnd(const Clip& clip);

Clip moveClip(Clip clip, Ticks start);
// The copy gets a new id.
Clip duplicateClip(Clip clip, Ticks start);

// Each edit returns the edited clip or a message saying why the edit cannot happen. MIDI edits keep only the notes
// and events that start inside the clip, end a note held across a cut at the cut and drop note-offs whose note-on was
// cut.

// Moves the left edge to start and keeps the end and the material where they were on the timeline.
std::expected<Clip, std::string> trimStart(const Clip& clip, Ticks start, const core::TempoMap& tempo,
                                           double sampleRate);
std::expected<Clip, std::string> setLength(const Clip& clip, Ticks length);
// Loops a MIDI clip's content until length on the timeline, with a partial last repeat where it falls short; a length
// up to the content's own ends the loop. The content keeps its length.
std::expected<Clip, std::string> setLoopLength(const Clip& clip, Ticks length);
// The right part gets a new id. Splitting a looped clip turns the repeats on each side into the content they play.
std::expected<std::pair<Clip, Clip>, std::string> split(const Clip& clip, Ticks at, const core::TempoMap& tempo,
                                                        double sampleRate);

// Crossfades the audio clip with the clip on its track that overlaps its start, or else its end: the earlier clip
// fades out and the later one fades in over the overlap, both with equal power.
std::expected<void, std::string> crossfade(Track& track, const ClipId& id);

}  // namespace emerick::model
