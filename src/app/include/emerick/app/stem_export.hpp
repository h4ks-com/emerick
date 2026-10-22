#pragma once

#include <filesystem>
#include <vector>

#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The end of the last clip on any track; exports render from bar 1 to here.
core::Ticks projectEnd(const model::Document& document);

// Renders each audio track of the session alone and dry, with its clip gains, fades and track gain, and each bus with
// the tracks of the session that feed it, to <folder>/<track name>.wav, or "<folder>/01 <track name>.wav" numbered by
// track position. Each stem runs on its own offline engine. Throws AppError when nothing can render.
std::vector<std::filesystem::path> exportStems(const engine::Session& session, const model::Document& document,
                                               const std::filesystem::path& folder, double sampleRate,
                                               bool numbered = false);

// Renders every audio track and bus of the session with sends, mute and solo applied to one stereo WAV.
void exportMix(const engine::Session& session, const model::Document& document, const std::filesystem::path& file,
               double sampleRate);

}  // namespace emerick::app
