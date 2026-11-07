#include "emerick/app/tuner.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace emerick::app {

namespace {

constexpr const char* kTapOwner = "tuner";
constexpr SourceRule kSourceRule{.liveInput = true};
constexpr double kFrameSeconds = 0.064;
constexpr int kHopsPerFrame = 4;
constexpr int kTickMs = 33;
constexpr auto kSilentAfter = std::chrono::milliseconds{250};
constexpr int kA4 = 69;
constexpr double kSemitones = 12.0;
constexpr double kCents = 100.0;
constexpr double kStateDecimals = 10.0;
constexpr double kDefaultReferenceHz = 440.0;

double rounded(double value) { return std::round(value * kStateDecimals) / kStateDecimals; }

// Keeps the last frame of samples on the reader's thread and measures it every hop.
struct PitchFrames {
    std::vector<float> history;
    std::vector<float> frame;
    std::size_t write = 0;
    std::size_t hop = 0;
    std::size_t untilHop = 0;
    double sampleRate = 0.0;

    template <class Publish>
    void add(std::span<const float> samples, const Publish& publish) {
        for (const float sample : samples) {
            history[write] = sample;
            write = (write + 1) % history.size();
            if (--untilHop == 0) {
                untilHop = hop;
                const auto split = history.begin() + static_cast<std::ptrdiff_t>(write);
                std::copy(history.begin(), split, std::copy(split, history.end(), frame.begin()));
                publish(engine::detectPitch(frame, sampleRate));
            }
        }
    }
};

}  // namespace

Tuner::Tuner(Registry& registry, ProjectController& controller, Nav& nav, QObject* parent)
    : QObject(parent), registry_(registry), controller_(controller), nav_(nav) {
    timer_.setInterval(kTickMs);
    connect(&timer_, &QTimer::timeout, this, &Tuner::tick);
    connect(&nav_, &Nav::changed, this, &Tuner::follow);
    connect(&controller_, &ProjectController::projectChanged, this, [this] {
        follow();
        emit sourcesChanged();
    });
}

QString Tuner::note() const { return needle_.shown() ? qs(engine::pitchClassName(nearestNote())) : QString{}; }

int Tuner::octave() const { return engine::noteOctave(nearestNote()); }

double Tuner::cents() const { return needle_.shown() ? (needle_.semitones() - nearestNote()) * kCents : 0.0; }

double Tuner::hz() const {
    return needle_.shown() ? referenceHz() * std::exp2((needle_.semitones() - kA4) / kSemitones) : 0.0;
}

QVariantList Tuner::sources() const { return sourceChoices(controller_.project().document(), kSourceRule); }

int Tuner::nearestNote() const { return static_cast<int>(std::lround(needle_.semitones())); }

double Tuner::referenceHz() const {
    const Json value = registry_.settingValue(kTunerReferenceSetting);
    return value.is_number() ? value.get<double>() : kDefaultReferenceHz;
}

void Tuner::setSource(const std::string& id) {
    checkSourceId(controller_.project().document(), id);
    chosen_ = id;
    emit sourceChanged();
    follow();
}

void Tuner::follow() {
    const std::optional<MonitorSource> wanted =
        nav_.tunerOpen()
            ? resolveSource(controller_.project().document(), chosen_, nav_.selectedTrack().toStdString(), kSourceRule)
            : std::nullopt;
    if (nav_.tunerOpen() != timer_.isActive()) {
        if (nav_.tunerOpen()) {
            lastTick_ = std::chrono::steady_clock::now();
            timer_.start();
        } else {
            timer_.stop();
        }
    }
    if (wanted == source_ && (reader_ != nullptr) == wanted.has_value()) {
        return;
    }
    source_ = wanted;
    needle_ = {};
    if (!wanted) {
        reader_.reset();
        controller_.setTap(kTapOwner, nullptr);
    } else {
        auto tap = std::make_shared<engine::SignalTap>(wanted->trackId, wanted->point);
        const double sampleRate = controller_.sampleRate();
        const auto frame = std::bit_ceil(static_cast<std::size_t>(sampleRate * kFrameSeconds));
        PitchFrames frames{.history = std::vector<float>(frame),
                           .frame = std::vector<float>(frame),
                           .hop = frame / kHopsPerFrame,
                           .untilHop = frame / kHopsPerFrame,
                           .sampleRate = sampleRate};
        reader_ = std::make_unique<TapReader>(tap, [this, frames](std::span<const float> samples) mutable {
            frames.add(samples, [this](const std::optional<engine::PitchReading>& reading) {
                const std::scoped_lock lock(readingsMutex_);
                readings_.push_back(reading);
            });
        });
        controller_.setTap(kTapOwner, std::move(tap));
    }
    emit sourceChanged();
    emit readingChanged();
}

void Tuner::tick() {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - std::exchange(lastTick_, now)).count();
    std::vector<std::optional<engine::PitchReading>> readings;
    {
        const std::scoped_lock lock(readingsMutex_);
        readings.swap(readings_);
    }
    if (!readings.empty()) {
        lastReading_ = now;
        for (const std::optional<engine::PitchReading>& reading : readings) {
            needle_.feed(reading, seconds / static_cast<double>(readings.size()), referenceHz());
        }
    } else if (now - lastReading_ > kSilentAfter) {
        needle_.feed(std::nullopt, seconds, referenceHz());
    }
    emit readingChanged();
}

Json Tuner::state() const {
    return {{"open", nav_.tunerOpen()},
            {"source", sourceJson(source_)},
            {"chosenSource", chosen_},
            {"referenceHz", referenceHz()},
            {"note", needle_.shown() ? Json(engine::noteName(nearestNote())) : Json(nullptr)},
            {"cents", rounded(cents())},
            {"hz", rounded(hz())},
            {"confidence", needle_.shown() ? rounded(needle_.confidence()) : 0.0},
            {"holding", needle_.holding()}};
}

}  // namespace emerick::app
