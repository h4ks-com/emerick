#pragma once

#include <QString>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

#include "emerick/app/variant.hpp"
#include "emerick/engine/signal_tap.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

// The source id that names the master; an empty source id follows the monitor's default.
inline constexpr const char* kMasterSource = "master";

// What a monitor listens to: a track's input or output, or the master when trackId is empty.
struct MonitorSource {
    std::string trackId;
    engine::TapPoint point = engine::TapPoint::output;
    std::string name;
    bool operator==(const MonitorSource&) const = default;
};

struct SourceRule {
    // The default when no source is chosen or the chosen track is gone: the master, else the selected track.
    bool masterByDefault = false;
    // An audio track that is armed or monitored is heard at its live input, which is what a player tunes from.
    bool liveInput = false;
};

std::optional<MonitorSource> resolveSource(const model::Document& document, const std::string& chosen,
                                           const std::string& selectedTrack, SourceRule rule);
// The id, the name and the point of a source, or null for none.
Json sourceJson(const std::optional<MonitorSource>& source);
// The picker's entries as {id, name}: the default first, then the master, then every track in order.
QVariantList sourceChoices(const model::Document& document, const QString& defaultName);
// Throws AppError when the id names no track and is neither empty nor the master.
void checkSourceId(const model::Document& document, const std::string& id);

// Starts a thread that drains the tap every few milliseconds and hands each chunk it reads to analyze, which runs on
// that thread, until this is destroyed.
class TapReader {
public:
    TapReader(std::shared_ptr<engine::SignalTap> tap, std::function<void(std::span<const float>)> analyze);
    [[nodiscard]] const std::shared_ptr<engine::SignalTap>& tap() const { return tap_; }

private:
    std::shared_ptr<engine::SignalTap> tap_;
    std::jthread thread_;
};

}  // namespace emerick::app
