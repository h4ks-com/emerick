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
Ticks clipEnd(const Clip& clip);

struct NoteSpan {
    Ticks at = 0;  // relative to the clip start
    Ticks length = 0;
    int key = 0;
};

// Pairs each note-on with its note-off; a note still held at the clip end lasts until the end.
std::vector<NoteSpan> noteSpans(const MidiClip& clip);

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

// Crossfades the audio clip with the clip on its track that overlaps its start, or else its end: the earlier clip
// fades out and the later one fades in over the overlap, both with equal power.
std::expected<void, std::string> crossfade(Track& track, const ClipId& id);

}  // namespace emerick::model
