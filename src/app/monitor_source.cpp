// The tap reader's thread drains its tap and runs the monitor's analysis, so the audio thread only copies samples.
#include "emerick/app/monitor_source.hpp"

#include <QVariantMap>
#include <array>
#include <chrono>
#include <utility>

#include "emerick/app/registry.hpp"

namespace emerick::app {

namespace {

constexpr auto kReadInterval = std::chrono::milliseconds{4};
constexpr std::size_t kReadChunk = 4096;

std::optional<MonitorSource> trackSource(const model::Track& track, SourceRule rule) {
    const bool live = rule.liveInput && (track.armed || track.monitor) && !track.audioIn.deviceId.empty();
    return MonitorSource{.trackId = track.id.value,
                         .point = live ? engine::TapPoint::input : engine::TapPoint::output,
                         .name = track.name + (live ? " input" : "")};
}

}  // namespace

std::optional<MonitorSource> resolveSource(const model::Document& document, const std::string& chosen,
                                           const std::string& selectedTrack, SourceRule rule) {
    if (chosen == kMasterSource) {
        return MonitorSource{.trackId = {}, .point = engine::TapPoint::output, .name = "Master"};
    }
    if (const model::Track* track = model::findTrack(document, model::TrackId{chosen}); track != nullptr) {
        return trackSource(*track, rule);
    }
    if (rule.masterByDefault) {
        return MonitorSource{.trackId = {}, .point = engine::TapPoint::output, .name = "Master"};
    }
    if (const model::Track* track = model::findTrack(document, model::TrackId{selectedTrack}); track != nullptr) {
        return trackSource(*track, rule);
    }
    return std::nullopt;
}

Json sourceJson(const std::optional<MonitorSource>& source) {
    if (!source) {
        return nullptr;
    }
    return {{"track", source->trackId.empty() ? Json(kMasterSource) : Json(source->trackId)},
            {"name", source->name},
            {"point", source->point == engine::TapPoint::input ? "input" : "output"}};
}

QVariantList sourceChoices(const model::Document& document, SourceRule rule) {
    QVariantList choices{QVariantMap{{"id", QString{}}, {"name", rule.masterByDefault ? "Master" : "Selected track"}}};
    if (!rule.masterByDefault) {
        choices.append(QVariantMap{{"id", qs(kMasterSource)}, {"name", QStringLiteral("Master")}});
    }
    for (const model::Track& track : document.tracks) {
        choices.append(QVariantMap{{"id", qs(track.id.value)}, {"name", qs(track.name)}});
    }
    return choices;
}

void checkSourceId(const model::Document& document, const std::string& id) {
    if (!id.empty() && id != kMasterSource && model::findTrack(document, model::TrackId{id}) == nullptr) {
        throw AppError("no track " + id + "; pass a track id, master, or an empty source for the default");
    }
}

TapReader::TapReader(std::shared_ptr<engine::SignalTap> tap, std::function<void(std::span<const float>)> analyze)
    : tap_(std::move(tap)), thread_([ring = &tap_->ring, analyze = std::move(analyze)](const std::stop_token& stop) {
          std::array<float, kReadChunk> chunk{};
          while (!stop.stop_requested()) {
              const std::size_t count = ring->read(chunk);
              if (count == 0) {
                  std::this_thread::sleep_for(kReadInterval);
                  continue;
              }
              analyze(std::span(chunk).first(count));
          }
      }) {}

}  // namespace emerick::app
