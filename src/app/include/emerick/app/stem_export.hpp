#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The end of the last clip on any track.
core::Ticks projectEnd(const model::Document& document);

// Where exports of the session stop: the project end, or the end of the bar of the last MIDI event a track plays, such
// as a chord from a lane, when that event comes after the project end.
core::Ticks exportEnd(const engine::Session& session, const model::Document& document);

// Renders each audio track and each MIDI track with an instrument of the session alone and dry, with its clip gains,
// fades and track gain, and each bus with the tracks of the session that feed it, to <folder>/<track name>.wav, or
// "<folder>/01 <track name>.wav" numbered by track position. A track whose instrument takes MIDI from another track
// plays with that track, and a track that only sends MIDI on gets no stem. Each stem runs on its own offline engine
// with fresh instances of the instruments from the slots of context's document. Throws AppError when nothing can
// render.
std::vector<std::filesystem::path> exportStems(const engine::Session& session, const Registry& registry,
                                               const PlaybackContext& context, const std::filesystem::path& folder,
                                               double sampleRate, bool numbered = false);

// Renders every audio track, instrument track and bus of the session with sends, mute and solo applied to one stereo
// WAV.
void exportMix(const engine::Session& session, const Registry& registry, const PlaybackContext& context,
               const std::filesystem::path& file, double sampleRate);

// Renders frames from the start of one track as its stem plays, or of the mix when trackId is empty.
std::vector<std::vector<float>> renderTrack(const engine::Session& session, const Registry& registry,
                                            const PlaybackContext& context, const std::string& trackId,
                                            engine::Frames frames, double sampleRate);

}  // namespace emerick::app
