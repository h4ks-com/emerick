#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <span>

#include "emerick/app/track_params.hpp"
#include "emerick/core/time.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// What a track row needs beside the track: the tempo, the rate that turns a clip's length into frames, and the folder
// that relative audio paths start from.
struct RowContext {
    core::TempoMap tempo;
    double sampleRate = 0.0;
    std::filesystem::path folder;
};

// The clip and note maps the tracks view reads from a track row. We shift each note by from, which places a take
// chunk's notes within the take.
QVariantList noteList(const model::MidiClip& clip, core::Ticks from = 0);
QVariantMap clipEntry(const model::Clip& clip, int take, const RowContext& context);
QVariantList clipList(const model::Track& track, const RowContext& context);
QVariantList laneList(const model::Track& track);
QVariantList paramList(std::span<const AutomationParam> params);

}  // namespace emerick::app
