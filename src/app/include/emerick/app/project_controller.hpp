#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/app/track_meters.hpp"
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
    Q_PROPERTY(QString streamError READ streamError NOTIFY transportChanged)
    Q_PROPERTY(QString tempoText READ tempoText NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerBeat READ ticksPerBeat NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerBar READ ticksPerBar NOTIFY projectChanged)
    Q_PROPERTY(qint64 ticksPerQuarter READ ticksPerQuarter CONSTANT)
    Q_PROPERTY(qint64 endTicks READ endTicks NOTIFY projectChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap routeDevices READ routeDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap trackErrors READ trackErrors NOTIFY trackErrorsChanged)
    Q_PROPERTY(QVariantList healthIssues READ healthIssues NOTIFY healthChanged)
    Q_PROPERTY(int midiChannelCount READ midiChannelCount CONSTANT)
    Q_PROPERTY(int trackColorCount READ trackColorCount CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY fileChanged)
    Q_PROPERTY(bool edited READ edited NOTIFY fileChanged)

public:
    // New projects go to projectsFolder, by default Emerick in the platform's music folder. When the app quits we
    // leave the project the way newProject does.
    ProjectController(const Registry& registry, std::unique_ptr<engine::AudioBackend> audio,
                      std::unique_ptr<engine::MidiBackend> midi, engine::Engine::Options options = {},
                      const std::filesystem::path& projectsFolder = {}, QObject* parent = nullptr);

    [[nodiscard]] model::Project& project() { return project_; }
    [[nodiscard]] const model::Project& project() const { return project_; }
    [[nodiscard]] engine::Engine& engine() { return engine_; }
    [[nodiscard]] const engine::Engine& engine() const { return engine_; }
    // The project bundle, a folder named "<name>.erk" holding project.json and audio/.
    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }
    [[nodiscard]] const std::filesystem::path& projectsFolder() const { return projectsFolder_; }
    // An untitled project keeps the name we gave it until saveAs names it.
    [[nodiscard]] bool untitled() const { return untitled_; }
    [[nodiscard]] QString name() const;
    // Edited means project.json lags the document; autosave writes it about 2 s after the last edit.
    [[nodiscard]] bool edited() const { return edited_; }
    [[nodiscard]] double sampleRate() const { return options_.sampleRate; }
    [[nodiscard]] const std::string& sessionError() const { return sessionError_; }
    // A damaged take still becomes a clip when the engine kept any of it, and its error stays listed here.
    [[nodiscard]] const std::vector<engine::SessionError>& takeErrors() const { return takeErrors_; }
    // Tracks whose audio device is gone; they play without that route.
    [[nodiscard]] const std::vector<engine::SessionError>& routeErrors() const { return routeErrors_; }

    // Each of these first stops the transport and keeps every pending take in the current project. Leaving a
    // project saves it, and an untitled project with nothing in it is deleted.
    void newProject();
    // Takes the bundle or the project.json inside it.
    void open(const std::filesystem::path& path);
    void save();
    // Adds the .erk suffix when it is missing. An untitled bundle moves to the new path, any other is copied.
    void saveAs(const std::filesystem::path& path);
    // Deletes the files in audio/ that no clip uses and returns their names.
    std::vector<std::string> removeUnusedAudio();

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
    void setLivePan(const model::TrackId& id, double pan);

    // The metronome plays from these settings once the next flush publishes them.
    void setMetronome(const engine::Metronome& metronome);
    // Playback and exports read the pan law from the registry; a new law reaches playback at the next flush.
    void panLawChanged(engine::PanLaw law);

    // Returns how many finished takes it added, one undo step each.
    int collectTakes();

    // Project edits reach the engine and the views once per event loop turn; flush sends a pending one right away.
    void flush();
    // Tests turn this off, since whether the app is active depends on the host and not on the test.
    void setWarmWhileActive(bool enabled) { warmWhileActive_ = enabled; }
    // Lists the devices again; we also do this whenever a backend reports a device change.
    void refreshDevices();

    [[nodiscard]] QString positionText() const;
    [[nodiscard]] qint64 positionTicks() const { return engine_.position().ticks; }
    [[nodiscard]] bool playing() const { return engine_.playing(); }
    [[nodiscard]] bool recording() const { return recording_; }
    // Set while the audio device stopped calling back, until it runs again.
    [[nodiscard]] QString streamError() const;
    [[nodiscard]] QString tempoText() const;
    [[nodiscard]] qint64 ticksPerBeat() const { return project_.document().tempo.ticksPerBeat(); }
    [[nodiscard]] qint64 ticksPerBar() const { return project_.document().tempo.ticksPerBar(); }
    [[nodiscard]] static qint64 ticksPerQuarter() { return core::kTicksPerQuarter; }
    [[nodiscard]] qint64 endTicks() const;
    // The nearest beat when the edit.snap setting is on, else ticks unchanged; never before bar 1.
    [[nodiscard]] qint64 snapped(qint64 ticks) const;
    // Where a drag or click puts ticks: snapped as edit.snap says, or the other way while Ctrl (Command on macOS) is
    // held, as Logic does with Control.
    Q_INVOKABLE qint64 placed(qint64 ticks, int modifiers) const;
    // The same on a grid of step ticks, such as the piano roll's.
    Q_INVOKABLE qint64 placedOn(qint64 ticks, int modifiers, qint64 step) const;
    // Each marker's state in position order.
    [[nodiscard]] QVariantList markers() const;
    [[nodiscard]] QVariantMap routeDevices() const;
    // A position as bar.beat.tick, or with duration a length counted from zero, as the number fields show them.
    Q_INVOKABLE QString ticksText(qint64 ticks, bool duration) const;
    // Reads text in the same form, or plain ticks; returns -1 for text it cannot read.
    Q_INVOKABLE qint64 parseTicks(const QString& text, bool duration) const;
    // Points (progress, gain) along a fade in of the shape and curve, from 0 to 1, for a clip to draw.
    Q_INVOKABLE static QVariantList fadeCurve(const QString& shape, double curve);
    Q_INVOKABLE static double curveThroughMidpoint(const QString& shape, double gain);
    [[nodiscard]] TrackMeters& meters() { return meters_; }
    [[nodiscard]] const TrackMeters& meters() const { return meters_; }
    // Per track id, the reasons its routes do not play, one per line.
    [[nodiscard]] QVariantMap trackErrors() const { return trackErrors_; }
    Json checkHealth();
    [[nodiscard]] QVariantList healthIssues() const { return healthIssues_; }
    [[nodiscard]] static int midiChannelCount() { return kMidiChannels; }
    [[nodiscard]] static int trackColorCount() { return kTrackColors; }

signals:
    // Play and record emit this just before the engine starts, so a listener can still send MIDI ahead of Start.
    void aboutToPlay();
    void transportChanged();
    void projectChanged();
    void devicesChanged();
    void trackErrorsChanged();
    void healthChanged();
    // The folder, the name or the edited flag changed.
    void fileChanged();

private:
    void createUntitled();
    void leave();
    void discardIfEmpty();
    void write();
    void autosave();
    void setEdited(bool value);
    void scheduleFlush();
    void settleTakes();
    void waitForEngine(const std::function<bool()>& reached);
    void startEngine();
    void publish();
    void updateTrackErrors();
    void keepWarmWhileActive();
    void logStreamChanges();
    void watchStream();
    void poll();
    void readLevels();
    void addTake(const engine::RecordedTake& take);
    void moveTo(const std::filesystem::path& folder, model::Document document);
    [[nodiscard]] static qint64 snappedIf(bool snap, qint64 ticks, qint64 step);

    const Registry& registry_;
    engine::Engine::Options options_;
    engine::Engine engine_;
    model::Project project_;
    std::filesystem::path projectsFolder_;
    std::filesystem::path folder_;
    bool untitled_ = false;
    bool edited_ = false;
    QTimer autosaveTimer_;
    std::set<std::string> sessionTracks_;
    std::set<std::string> sessionAudioTracks_;
    std::set<std::string> pendingAudioTakes_;
    std::string sessionError_;
    std::vector<engine::SessionError> takeErrors_;
    std::vector<engine::SessionError> routeErrors_;
    engine::Metronome metronome_;
    engine::PanLaw panLaw_ = engine::PanLaw::compensated;
    bool recording_ = false;
    bool flushPending_ = false;
    core::Ticks stopTicks_ = 0;
    core::Ticks lastTicks_ = -1;
    bool lastPlaying_ = false;
    TrackMeters meters_;
    std::chrono::steady_clock::time_point lastLevels_ = std::chrono::steady_clock::now();
    bool lastStalled_ = false;
    QVariantMap trackErrors_;
    QVariantList healthIssues_;
    std::optional<std::chrono::steady_clock::time_point> lastActive_;
    bool warm_ = false;
    bool warmWhileActive_ = true;
    QString streamSummary_;
    QTimer timer_;
};

}  // namespace emerick::app
