#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <functional>
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

QVariantMap audioRouteEntry(const model::AudioRoute& route);
QVariantMap midiRouteEntry(const model::MidiRoute& route);
// Each send with its bus's name and the level the given function reads, which follows automation in the app.
QVariantList sendList(const model::Track& track, const model::Document& document,
                      const std::function<double(const model::Send&)>& level);
QVariantList insertList(const model::Track& track);
// The MIDI clip's points on a controller lane, flat as at, value, at, value, or empty for any other lane.
QList<double> controlPointList(const model::MidiClip& clip, const QString& lane);

// The instruments that ship with the app. Their CLAP state is JSON.
inline constexpr const char* kGeneralMidiPluginId = "org.emerick.gm";
inline constexpr const char* kVoicesPluginId = "org.emerick.voices";
inline constexpr const char* kArrangerPluginId = "org.emerick.arranger";

// The track's instrument as {name, format, sound}: the sound a bundled instrument's saved state picks, such as the
// General MIDI sound or the Expansion Voices voice, or empty. A track without an instrument gives an empty map.
QVariantMap instrumentEntry(const model::Track& track);

}  // namespace emerick::app
