#pragma once

#include <QObject>
#include <QString>

#include "emerick/app/project_controller.hpp"

namespace emerick::app {

inline constexpr core::Ticks kTicksPerDivision = core::kTicksPerQuarter / 4;

// The control bar display: the playhead as Logic counts it, from 1.1.1.1, and the tempo and meter as numbers.
class TransportClock : public QObject {
    Q_OBJECT
    Q_PROPERTY(int bar READ bar NOTIFY changed)
    Q_PROPERTY(int beat READ beat NOTIFY changed)
    Q_PROPERTY(int division READ division NOTIFY changed)
    Q_PROPERTY(int tick READ tick NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(QString meter READ meter NOTIFY changed)

public:
    explicit TransportClock(const ProjectController& controller, QObject* parent = nullptr);

    [[nodiscard]] int bar() const { return position().bar; }
    [[nodiscard]] int beat() const { return position().beat; }
    [[nodiscard]] int division() const { return static_cast<int>(position().tick / kTicksPerDivision) + 1; }
    [[nodiscard]] int tick() const { return static_cast<int>(position().tick % kTicksPerDivision) + 1; }
    [[nodiscard]] double bpm() const { return controller_.project().document().tempo.bpm; }
    [[nodiscard]] QString meter() const;

signals:
    void changed();

private:
    [[nodiscard]] core::BarBeatTick position() const;

    const ProjectController& controller_;
};

}  // namespace emerick::app
