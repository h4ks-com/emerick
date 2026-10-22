#include "emerick/app/track_meters.hpp"

#include <QQmlEngine>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>

namespace emerick::app {

namespace {

constexpr double kReleaseDbPerSecond = 20.0;
constexpr double kHoldSeconds = 2.0;
constexpr double kStateDecimals = 10.0;

struct ScalePoint {
    double db;
    double fraction;
};

// Each 10 dB step below -20 dB gets less height, close to the IEC 60268-18 scale.
constexpr std::array kScale{ScalePoint{kMeterFloorDb, 0.0}, ScalePoint{-50.0, 0.05},  ScalePoint{-40.0, 0.125},
                            ScalePoint{-30.0, 0.275},       ScalePoint{-20.0, 0.475}, ScalePoint{0.0, 1.0}};

double toDb(float peak) {
    return peak > 0.0F ? std::max(20.0 * std::log10(static_cast<double>(peak)), kMeterFloorDb) : kMeterFloorDb;
}

QObject* exposed(TrackMeter& meter) {
    QQmlEngine::setObjectOwnership(&meter, QQmlEngine::CppOwnership);
    return &meter;
}

}  // namespace

double meterFraction(double db) {
    if (db <= kScale.front().db) {
        return 0.0;
    }
    if (db >= kScale.back().db) {
        return 1.0;
    }
    const auto* const upper = std::ranges::find_if(kScale, [db](const ScalePoint& point) { return point.db >= db; });
    const ScalePoint& lower = *std::prev(upper);
    return lower.fraction + (upper->fraction - lower.fraction) * (db - lower.db) / (upper->db - lower.db);
}

bool MeterChannel::feed(float peak, double seconds) {
    const double peakDb = toDb(peak);
    const double release = kReleaseDbPerSecond * seconds;
    const double levelDb = std::max(levelDb_ - release, peakDb);
    double holdDb = holdDb_;
    if (peakDb >= holdDb_) {
        holdDb = peakDb;
        holdLeft_ = kHoldSeconds;
    } else if (holdLeft_ > 0.0) {
        holdLeft_ -= seconds;
    } else {
        holdDb = std::max(holdDb_ - release, levelDb);
    }
    if (levelDb == levelDb_ && holdDb == holdDb_) {
        return false;
    }
    levelDb_ = levelDb;
    holdDb_ = holdDb;
    emit changed();
    return true;
}

void TrackMeter::feed(const std::vector<float>& peaks, bool clipped, double seconds) {
    bool moved = std::cmp_not_equal(channels_.size(), peaks.size());
    if (moved) {
        for (QObject* channel : std::exchange(channels_, {})) {
            channel->deleteLater();
        }
        for (std::size_t index = 0; index < peaks.size(); ++index) {
            // The meter owns its channels as their Qt parent.
            channels_.append(std::make_unique<MeterChannel>(this).release());
        }
        emit channelsChanged();
    }
    for (std::size_t index = 0; index < peaks.size(); ++index) {
        moved |= qobject_cast<MeterChannel*>(channels_.at(static_cast<qsizetype>(index)))->feed(peaks[index], seconds);
    }
    if (moved) {
        emit peakChanged();
    }
    if (clipped && !clipped_) {
        clipped_ = true;
        emit clippedChanged();
    }
}

void TrackMeter::resetClip() {
    if (clipped_) {
        clipped_ = false;
        emit clippedChanged();
    }
}

double TrackMeter::peakDb() const {
    double peak = kMeterFloorDb;
    for (const QObject* channel : channels_) {
        peak = std::max(peak, qobject_cast<const MeterChannel*>(channel)->holdDb());
    }
    return peak;
}

QObject* TrackMeters::input(const QString& trackId) { return exposed(meter(inputs_, trackId.toStdString())); }

QObject* TrackMeters::output(const QString& trackId) { return exposed(meter(outputs_, trackId.toStdString())); }

TrackMeter& TrackMeters::meter(Meters& meters, const std::string& trackId) {
    std::unique_ptr<TrackMeter>& found = meters[trackId];
    if (!found) {
        found = std::make_unique<TrackMeter>();
    }
    return *found;
}

void TrackMeters::update(Meters& meters, const std::vector<engine::LevelReading>& readings, double seconds) {
    for (auto& [trackId, trackMeter] : meters) {
        if (!std::ranges::contains(readings, trackId, &engine::LevelReading::trackId)) {
            trackMeter->feed({}, false, seconds);
        }
    }
    for (const engine::LevelReading& reading : readings) {
        meter(meters, reading.trackId).feed(reading.peaks, reading.clipped, seconds);
    }
}

void TrackMeters::update(const std::vector<engine::LevelReading>& inputs,
                         const std::vector<engine::LevelReading>& outputs, double seconds) {
    update(inputs_, inputs, seconds);
    update(outputs_, outputs, seconds);
}

void TrackMeters::updateReductions(const std::vector<engine::GainReduction>& readings) {
    QVariantMap next;
    for (const engine::GainReduction& reading : readings) {
        next.insert(qs(reading.insertId), reading.db);
    }
    if (next != reductions_) {
        reductions_ = std::move(next);
        emit reductionsChanged();
    }
}

void TrackMeters::resetClips() {
    for (Meters* meters : {&inputs_, &outputs_}) {
        for (auto& entry : *meters) {
            entry.second->resetClip();
        }
    }
}

void TrackMeters::resetClips(const std::string& trackId) {
    for (Meters* meters : {&inputs_, &outputs_}) {
        if (const auto found = meters->find(trackId); found != meters->end()) {
            found->second->resetClip();
        }
    }
}

Json TrackMeters::state() const {
    Json state = Json::object();
    const auto add = [&](const Meters& meters, const char* key) {
        for (const auto& [trackId, trackMeter] : meters) {
            if (!trackMeter->channels().isEmpty()) {
                state[trackId][key] = {{"peakDb", std::round(trackMeter->peakDb() * kStateDecimals) / kStateDecimals},
                                       {"clipped", trackMeter->clipped()}};
            }
        }
    };
    add(inputs_, "input");
    add(outputs_, "output");
    return state;
}

}  // namespace emerick::app
