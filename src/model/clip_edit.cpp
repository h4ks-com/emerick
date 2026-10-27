#include "emerick/model/clip_edit.hpp"

#include <algorithm>
#include <ranges>
#include <variant>
#include <vector>

#include "emerick/core/overloaded.hpp"
#include "emerick/model/midi_notes.hpp"

namespace emerick::model {

namespace {

// Keeps the notes and events that start in [from, to), moved so from becomes 0, and ends each note by to.
MidiClip cutMidi(MidiClip clip, Ticks from, Ticks to) {
    gatherNotes(clip);
    const auto outside = [&](Ticks at) { return at < from || at >= to; };
    std::erase_if(clip.events, [&](const MidiEvent& event) { return outside(event.at); });
    std::erase_if(clip.notes, [&](const Note& note) { return outside(note.start); });
    for (MidiEvent& event : clip.events) {
        event.at -= from;
    }
    for (Note& note : clip.notes) {
        note.start -= from;
        note.length = std::min(note.length, to - from - note.start);
    }
    return clip;
}

std::string lengthError() { return "a clip needs a length above 0 ticks"; }

const MidiClip* loopingMidi(const Clip& clip) {
    return std::visit(
        core::Overloaded{[](const AudioClip&) -> const MidiClip* { return nullptr; },
                         [](const MidiClip& midi) -> const MidiClip* { return looped(midi) ? &midi : nullptr; }},
        clip);
}

// The left part keeps looping the content up to the split. The right part loops the content too when the split falls
// on a repeat boundary, and otherwise holds the repeats it covers written out.
std::pair<Clip, Clip> splitLooped(const MidiClip& clip, Ticks at) {
    const Ticks offset = at - clip.start;
    const Ticks end = clip.start + clip.loopLength;
    MidiClip left = clip;
    if (offset > clip.length) {
        left.loopLength = offset;
    } else {
        left = cutMidi(clip, 0, offset);
        left.length = offset;
        left.loopLength = 0;
    }
    MidiClip right = offset % clip.length == 0 ? clip : cutMidi(unrolled(clip), offset, clip.loopLength);
    right.id = ClipId::make();
    right.start = at;
    if (offset % clip.length == 0) {
        right.loopLength = end - at > clip.length ? end - at : 0;
    } else {
        right.length = end - at;
    }
    return {std::move(left), std::move(right)};
}

}  // namespace

Ticks clipStart(const Clip& clip) {
    return std::visit([](const auto& value) { return value.start; }, clip);
}

Ticks clipLength(const Clip& clip) {
    return std::visit([](const auto& value) { return value.length; }, clip);
}

Ticks clipEnd(const Clip& clip) {
    return clipStart(clip) + std::visit(core::Overloaded{[](const AudioClip& audio) { return audio.length; },
                                                         [](const MidiClip& midi) { return clipSpan(midi); }},
                                        clip);
}

Clip moveClip(Clip clip, Ticks start) {
    std::visit([start](auto& value) { value.start = start; }, clip);
    return clip;
}

Clip duplicateClip(Clip clip, Ticks start) {
    std::visit([](auto& value) { value.id = ClipId::make(); }, clip);
    return moveClip(std::move(clip), start);
}

std::expected<Clip, std::string> trimStart(const Clip& clip, Ticks start, const core::TempoMap& tempo,
                                           double sampleRate) {
    if (start < 0) {
        return std::unexpected("a clip cannot start before bar 1");
    }
    const Ticks delta = start - clipStart(clip);
    const Ticks length = clipLength(clip) - delta;
    if (length <= 0) {
        return std::unexpected(lengthError());
    }
    return std::visit(
        core::Overloaded{[&](AudioClip audio) -> std::expected<Clip, std::string> {
                             const core::Frames offset = audio.sourceOffset + tempo.ticksToFrames(start, sampleRate) -
                                                         tempo.ticksToFrames(audio.start, sampleRate);
                             if (offset < 0) {
                                 return std::unexpected("the clip cannot start before its audio file starts");
                             }
                             audio.sourceOffset = offset;
                             audio.start = start;
                             audio.length = length;
                             return audio;
                         },
                         [&](MidiClip midi) -> std::expected<Clip, std::string> {
                             const Ticks end = midi.length;
                             const Ticks span = clipSpan(midi) - delta;
                             midi = cutMidi(std::move(midi), delta, end);
                             midi.start = start;
                             midi.length = length;
                             midi.loopLength = span > length ? span : 0;
                             return midi;
                         }},
        clip);
}

std::expected<Clip, std::string> setLength(const Clip& clip, Ticks length) {
    if (length <= 0) {
        return std::unexpected(lengthError());
    }
    return std::visit(core::Overloaded{[&](AudioClip audio) -> Clip {
                                           audio.length = length;
                                           return audio;
                                       },
                                       [&](MidiClip midi) -> Clip {
                                           midi = cutMidi(std::move(midi), 0, length);
                                           midi.length = length;
                                           midi.loopLength = midi.loopLength > length ? midi.loopLength : 0;
                                           return midi;
                                       }},
                      clip);
}

std::expected<std::pair<Clip, Clip>, std::string> split(const Clip& clip, Ticks at, const core::TempoMap& tempo,
                                                        double sampleRate) {
    const Ticks start = clipStart(clip);
    const Ticks end = clipEnd(clip);
    if (at <= start || at >= end) {
        return std::unexpected("split position must be inside the clip, between " + std::to_string(start) + " and " +
                               std::to_string(end) + " ticks");
    }
    if (const MidiClip* midi = loopingMidi(clip)) {
        return splitLooped(*midi, at);
    }
    auto left = setLength(clip, at - start);
    auto right = trimStart(duplicateClip(clip, start), at, tempo, sampleRate);
    if (!left || !right) {
        return std::unexpected(left ? right.error() : left.error());
    }
    std::visit(core::Overloaded{[](AudioClip& audio) { audio.fadeOut = 0; }, [](MidiClip&) {}}, *left);
    std::visit(core::Overloaded{[](AudioClip& audio) { audio.fadeIn = 0; }, [](MidiClip&) {}}, *right);
    return std::pair{std::move(*left), std::move(*right)};
}

std::expected<Clip, std::string> setLoopLength(const Clip& clip, Ticks length) {
    if (length <= 0) {
        return std::unexpected(lengthError());
    }
    return std::visit(core::Overloaded{[](const AudioClip&) -> std::expected<Clip, std::string> {
                                           return std::unexpected("only MIDI clips loop");
                                       },
                                       [&](MidiClip midi) -> std::expected<Clip, std::string> {
                                           midi.loopLength = length > midi.length ? length : 0;
                                           return midi;
                                       }},
                      clip);
}

std::expected<void, std::string> crossfade(Track& track, const ClipId& id) {
    auto audioClips = track.clips | std::views::transform([](Clip& clip) { return std::get_if<AudioClip>(&clip); }) |
                      std::views::filter([](const AudioClip* audio) { return audio != nullptr; });
    const auto found = std::ranges::find(audioClips, id, &AudioClip::id);
    if (found == audioClips.end()) {
        return std::unexpected("a crossfade needs an audio clip");
    }
    AudioClip& clip = **found;
    const auto overlapsStart = [&](const AudioClip* other) {
        return other->start < clip.start && other->start + other->length > clip.start;
    };
    const auto overlapsEnd = [&](const AudioClip* other) {
        return other->start > clip.start && other->start < clip.start + clip.length;
    };
    AudioClip* earlier = &clip;
    AudioClip* later = &clip;
    if (const auto left = std::ranges::find_if(audioClips, overlapsStart); left != audioClips.end()) {
        earlier = *left;
    } else if (const auto right = std::ranges::find_if(audioClips, overlapsEnd); right != audioClips.end()) {
        later = *right;
    } else {
        return std::unexpected("overlap the clip with another clip on its track first");
    }
    const Ticks earlierEnd = earlier->start + earlier->length;
    if (earlierEnd > later->start + later->length) {
        return std::unexpected("the clip lies inside another clip, so shorten the longer clip first");
    }
    const Ticks overlap = earlierEnd - later->start;
    earlier->fadeOut = overlap;
    earlier->fadeOutShape = core::FadeShape::equalPower;
    earlier->fadeOutCurve = 0.0;
    earlier->fadeIn = std::min(earlier->fadeIn, earlier->length - overlap);
    later->fadeIn = overlap;
    later->fadeInShape = core::FadeShape::equalPower;
    later->fadeInCurve = 0.0;
    later->fadeOut = std::min(later->fadeOut, later->length - overlap);
    return {};
}

}  // namespace emerick::model
