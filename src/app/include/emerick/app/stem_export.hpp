#pragma once

#include <filesystem>
#include <vector>

#include "emerick/engine/session.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The end of the last clip on any track; exports render from bar 1 to here.
core::Ticks projectEnd(const model::Document& document);

// Renders each audio track of the session alone, with its clip gains, fades and track gain, to
// <folder>/<track name>.wav. Each stem runs on its own offline engine. Throws AppError when nothing can render.
std::vector<std::filesystem::path> exportStems(const engine::Session& session, const model::Document& document,
                                               const std::filesystem::path& folder, double sampleRate);

// Renders every audio track of the session with mute and solo applied to one stereo WAV.
void exportMix(const engine::Session& session, const model::Document& document, const std::filesystem::path& file,
               double sampleRate);

}  // namespace emerick::app
