#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/engine.hpp"

namespace emerick::app {

inline constexpr double kMeterFloorDb = -60.0;

// Where a level in dBFS sits on a meter, from 0 at the floor to 1 at full scale, with more room near the top.
double meterFraction(double db);

// One channel of a peak meter: instant attack, a 20 dB/s release and a peak held for 2 s, as SoundCraft's meters do.
class MeterChannel : public QObject {
    Q_OBJECT
    Q_PROPERTY(double levelDb READ levelDb NOTIFY changed)
    Q_PROPERTY(double holdDb READ holdDb NOTIFY changed)
    Q_PROPERTY(double level READ level NOTIFY changed)
    Q_PROPERTY(double hold READ hold NOTIFY changed)

public:
    using QObject::QObject;

    // Takes the loudest sample of the last seconds and returns whether the meter moved.
    bool feed(float peak, double seconds);
    [[nodiscard]] double levelDb() const { return levelDb_; }
    [[nodiscard]] double holdDb() const { return holdDb_; }
    [[nodiscard]] double level() const { return meterFraction(levelDb_); }
    [[nodiscard]] double hold() const { return meterFraction(holdDb_); }

signals:
    void changed();

private:
    double levelDb_ = kMeterFloorDb;
    double holdDb_ = kMeterFloorDb;
    double holdLeft_ = 0.0;
};

// A track's input or output level, one channel per bar, and a clip light that stays lit until reset.
class TrackMeter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QObject*> channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(bool clipped READ clipped NOTIFY clippedChanged)
    Q_PROPERTY(double peakDb READ peakDb NOTIFY peakChanged)

public:
    using QObject::QObject;

    // A reading with no peaks hides the meter.
    void feed(const std::vector<float>& peaks, bool clipped, double seconds);
    void resetClip();
    [[nodiscard]] const QList<QObject*>& channels() const { return channels_; }
    [[nodiscard]] bool clipped() const { return clipped_; }
    // The highest held peak of any channel.
    [[nodiscard]] double peakDb() const;

signals:
    void channelsChanged();
    void clippedChanged();
    void peakChanged();

private:
    QList<QObject*> channels_;
    bool clipped_ = false;
};

// Every track's input and output meter, fed from the engine on each poll.
class TrackMeters : public QObject {
    Q_OBJECT
    Q_PROPERTY(double floorDb READ floorDb CONSTANT)

public:
    using QObject::QObject;

    // The meters live as long as this, so QML may hold on to them.
    Q_INVOKABLE QObject* input(const QString& trackId);
    Q_INVOKABLE QObject* output(const QString& trackId);
    Q_INVOKABLE static double fraction(double db) { return meterFraction(db); }
    [[nodiscard]] static double floorDb() { return kMeterFloorDb; }

    void update(const std::vector<engine::LevelReading>& inputs, const std::vector<engine::LevelReading>& outputs,
                double seconds);
    void resetClips();
    void resetClips(const std::string& trackId);
    // Per track id with a visible meter: the held peak in dB and whether it clipped, for input and output.
    [[nodiscard]] Json state() const;

private:
    using Meters = std::map<std::string, std::unique_ptr<TrackMeter>, std::less<>>;
    static TrackMeter& meter(Meters& meters, const std::string& trackId);
    static void update(Meters& meters, const std::vector<engine::LevelReading>& readings, double seconds);

    Meters inputs_;
    Meters outputs_;
};

}  // namespace emerick::app
