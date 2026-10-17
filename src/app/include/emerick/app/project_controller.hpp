#pragma once

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

inline constexpr int kMidiChannels = 16;
inline constexpr int kTrackColors = 8;

class ProjectController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString positionText READ positionText NOTIFY transportChanged)
    Q_PROPERTY(qint64 positionTicks READ positionTicks NOTIFY transportChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
    Q_PROPERTY(QString tempoText READ tempoText NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap routeChoices READ routeChoices NOTIFY projectChanged)
    Q_PROPERTY(int midiChannelCount READ midiChannelCount CONSTANT)
    Q_PROPERTY(int trackColorCount READ trackColorCount CONSTANT)

public:
    ProjectController(const Registry& registry, std::unique_ptr<engine::AudioBackend> audio,
                      std::unique_ptr<engine::MidiBackend> midi, engine::Engine::Options options = {},
                      QObject* parent = nullptr);

    [[nodiscard]] model::Project& project() { return project_; }
    [[nodiscard]] const model::Project& project() const { return project_; }
    [[nodiscard]] engine::Engine& engine() { return engine_; }
    [[nodiscard]] const engine::Engine& engine() const { return engine_; }
    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }
    [[nodiscard]] bool untitled() const { return untitled_.has_value(); }
    [[nodiscard]] double sampleRate() const { return options_.sampleRate; }
    [[nodiscard]] const std::string& sessionError() const { return sessionError_; }
    // A damaged take still becomes a clip when the engine kept any of it, and its error stays listed here.
    [[nodiscard]] const std::vector<engine::SessionError>& takeErrors() const { return takeErrors_; }

    // Each of these first stops the transport and keeps every pending take in the current project.
    void newProject();
    void open(const std::filesystem::path& folder);
    void save();
    void saveAs(const std::filesystem::path& folder);

    // Each waits briefly until the engine reports the commanded state.
    void play();
    void stop();
    void record();
    void locate(core::Ticks ticks);

    // Live toggles reach the engine at the next block, before the rebuilt session does.
    void setLiveMute(const model::TrackId& id, bool value);
    void setLiveSolo(const model::TrackId& id, bool value);
    void setLiveArmed(const model::TrackId& id, bool value);
    void setLiveGain(const model::TrackId& id, double db);

    // Returns how many finished takes it added, one undo step each.
    int collectTakes();

    [[nodiscard]] QString positionText() const;
    [[nodiscard]] qint64 positionTicks() const { return engine_.position().ticks; }
    [[nodiscard]] bool playing() const { return engine_.playing(); }
    [[nodiscard]] bool recording() const { return recording_; }
    [[nodiscard]] QString tempoText() const;
    [[nodiscard]] QVariantMap routeChoices();
    [[nodiscard]] static int midiChannelCount() { return kMidiChannels; }
    [[nodiscard]] static int trackColorCount() { return kTrackColors; }

signals:
    // Play and record emit this just before the engine starts, so a listener can still send MIDI ahead of Start.
    void aboutToPlay();
    void transportChanged();
    void projectChanged();

private:
    void settleTakes();
    void waitForEngine(const std::function<bool()>& reached);
    void publish();
    void poll();
    void addTake(const engine::RecordedTake& take);
    void moveTo(const std::filesystem::path& folder, model::Document document);

    const Registry& registry_;
    engine::Engine::Options options_;
    engine::Engine engine_;
    model::Project project_;
    std::optional<QTemporaryDir> untitled_;
    std::filesystem::path folder_;
    std::set<std::string> sessionTracks_;
    std::set<std::string> sessionAudioTracks_;
    std::set<std::string> pendingAudioTakes_;
    std::string sessionError_;
    std::vector<engine::SessionError> takeErrors_;
    bool recording_ = false;
    core::Ticks stopTicks_ = 0;
    core::Ticks lastTicks_ = -1;
    bool lastPlaying_ = false;
    QTimer timer_;
};

}  // namespace emerick::app
