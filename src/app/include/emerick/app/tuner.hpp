#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/monitor_source.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/pitch.hpp"
#include "emerick/engine/pitch_needle.hpp"

namespace emerick::app {

inline constexpr const char* kTunerReferenceSetting = "tuner.referencePitch";

// Listens to one source while the tuner window is open and shows its pitch as a note and cents.
class Tuner : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY readingChanged)
    Q_PROPERTY(bool holding READ holding NOTIFY readingChanged)
    Q_PROPERTY(QString note READ note NOTIFY readingChanged)
    Q_PROPERTY(int octave READ octave NOTIFY readingChanged)
    Q_PROPERTY(double cents READ cents NOTIFY readingChanged)
    Q_PROPERTY(double hz READ hz NOTIFY readingChanged)
    Q_PROPERTY(QString sourceId READ sourceId NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)

public:
    Tuner(Registry& registry, ProjectController& controller, Nav& nav, QObject* parent = nullptr);

    [[nodiscard]] bool shown() const { return needle_.shown(); }
    [[nodiscard]] bool holding() const { return needle_.holding(); }
    [[nodiscard]] QString note() const;
    [[nodiscard]] int octave() const;
    [[nodiscard]] double cents() const;
    [[nodiscard]] double hz() const;
    [[nodiscard]] QString sourceId() const { return qs(chosen_); }
    [[nodiscard]] QString sourceName() const { return source_ ? qs(source_->name) : QString{}; }
    [[nodiscard]] QVariantList sources() const;

    // An empty id follows the selected track, kMasterSource the master.
    void setSource(const std::string& id);
    [[nodiscard]] Json state() const;

signals:
    void readingChanged();
    void sourceChanged();
    void sourcesChanged();

private:
    [[nodiscard]] double referenceHz() const;
    [[nodiscard]] int nearestNote() const;
    void follow();
    void tick();

    Registry& registry_;
    ProjectController& controller_;
    Nav& nav_;
    std::string chosen_;
    std::optional<MonitorSource> source_;
    std::mutex readingsMutex_;
    std::vector<std::optional<engine::PitchReading>> readings_;
    // Declared after what its thread writes, so it stops before they go.
    std::unique_ptr<TapReader> reader_;
    std::chrono::steady_clock::time_point lastTick_;
    std::chrono::steady_clock::time_point lastReading_;
    engine::PitchNeedle needle_;
    QTimer timer_;
};

}  // namespace emerick::app
