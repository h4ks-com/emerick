#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/monitor_source.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/spectrum.hpp"

namespace emerick::app {

inline constexpr const char* kSpectrogramFftSetting = "spectrogram.fftSize";
inline constexpr const char* kSpectrogramWindowSetting = "spectrogram.window";
inline constexpr const char* kSpectrogramCeilingSetting = "spectrogram.ceilingDb";
inline constexpr const char* kSpectrogramFloorSetting = "spectrogram.floorDb";
inline constexpr const char* kSpectrogramAxisSetting = "spectrogram.frequencyAxis";
inline constexpr const char* kSpectrogramSpectrumSetting = "spectrogram.showSpectrum";

// What the spectrogram settings hold, read from the registry.
struct SpectrogramSettings {
    std::size_t fftSize = 0;
    engine::SpectrumWindow window = engine::SpectrumWindow::blackmanHarris;
    double ceilingDb = 0.0;
    double floorDb = 0.0;
    bool logAxis = true;
    bool showSpectrum = true;
    bool operator==(const SpectrogramSettings&) const = default;
};

// The spectrogram settings, each a registry setting the window's controls and the settings page share.
void registerSpectrogramSettings(Registry& registry);
SpectrogramSettings spectrogramSettings(const Registry& registry);

// Listens to one source while the spectrogram window is open, turns it into rows of levels per FFT bin on the tap
// reader's thread, and keeps the loudest peaks of the newest row.
class Spectrogram : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceId READ sourceId NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)

public:
    // The newest rows the view has not drawn yet stay queued up to this many.
    static constexpr std::size_t kQueuedRows = 1024;
    static constexpr std::size_t kPeaks = 5;

    Spectrogram(Registry& registry, ProjectController& controller, Nav& nav, QObject* parent = nullptr);

    [[nodiscard]] QString sourceId() const { return qs(chosen_); }
    [[nodiscard]] QString sourceName() const { return source_ ? qs(source_->name) : QString{}; }
    [[nodiscard]] QVariantList sources() const;
    [[nodiscard]] const SpectrogramSettings& settings() const { return settings_; }
    [[nodiscard]] double sampleRate() const { return controller_.sampleRate(); }
    [[nodiscard]] double binHz() const;
    [[nodiscard]] double rowsPerSecond() const;

    // An empty id follows the master, as kMasterSource does.
    void setSource(const std::string& id);
    // Hands the view every row since the last call, oldest first.
    std::vector<std::vector<float>> takeRows();
    [[nodiscard]] std::vector<engine::SpectrumPeak> peaks() const;
    [[nodiscard]] Json state() const;

signals:
    void sourceChanged();
    void sourcesChanged();
    // The view starts over, because the rows it holds no longer match.
    void restarted();
    void settingsChanged();

private:
    void follow();
    void readSettings();

    Registry& registry_;
    ProjectController& controller_;
    Nav& nav_;
    std::string chosen_;
    SpectrogramSettings settings_;
    std::optional<MonitorSource> source_;
    std::size_t hop_ = 0;
    mutable std::mutex rowsMutex_;
    std::deque<std::vector<float>> rows_;
    std::vector<engine::SpectrumPeak> peaks_;
    // Declared after what its thread writes, so it stops before they go.
    std::unique_ptr<TapReader> reader_;
};

}  // namespace emerick::app
