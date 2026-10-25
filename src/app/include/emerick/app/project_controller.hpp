#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/peak_cache.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/track_meters.hpp"
#include "emerick/engine/engine.hpp"
#include "emerick/model/project.hpp"

namespace emerick::app {

inline constexpr int kMidiChannels = 16;
inline constexpr int kTrackColors = 8;

// A take in progress as the tracks area draws it, until it lands as a clip and one undo step.
struct TakePreview {
    std::uint64_t take = 0;
    core::Ticks start = 0;
    core::Ticks length = 0;
    std::filesystem::path audioFile;
    core::Frames frames = 0;
    engine::RecordedTake recorded;
    // The track as the take would leave it, for every kind but audio, once the take holds any MIDI.
    std::optional<model::Track> track;
};

// How a device follows playback, as a plugin reports it. The transport shows the short label, such as "Main B 3/4",
// or a warning icon when it is not ok, with the label and the multiline detail in a tooltip.
struct SyncStatus {
    std::string label;
    std::string detail;
    bool ok = true;
};

class ProjectController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString positionText READ positionText NOTIFY transportChanged)
    Q_PROPERTY(qint64 positionTicks READ positionTicks NOTIFY transportChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY transportChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
    Q_PROPERTY(QString streamError READ streamError NOTIFY transportChanged)
    Q_PROPERTY(QString startReason READ startReason NOTIFY transportChanged)
    Q_PROPERTY(QVariantList syncBadges READ syncBadges NOTIFY syncChanged)
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
    // Sends every track type's panic messages, then the engine's panic. It works while stopped.
    void panic();
    // A plugin reports under its own source name; the transport shows each status, and an empty label removes it.
    void setSyncStatus(const std::string& source, SyncStatus status);
    [[nodiscard]] const std::map<std::string, SyncStatus>& syncStatus() const { return syncStatus_; }
    // Why play started where it did, from a track type's alignStart, until the transport stops.
    [[nodiscard]] QString startReason() const { return startReason_; }
    [[nodiscard]] QVariantList syncBadges() const;

    // Live toggles reach the engine at the next block, before the rebuilt session does.
    void setLiveMute(const model::TrackId& id, bool value);
    void setLiveSolo(const model::TrackId& id, bool value);
    void setLiveArmed(const model::TrackId& id, bool value);
    void setLiveGain(const model::TrackId& id, double db);
    void setLivePan(const model::TrackId& id, double pan);

    // While the transport runs and the track is in Touch or Latch, a control move writes value into the param's lane
    // and returns true, and the caller leaves the track's own setting alone. The engine plays the written value until
    // the pass ends: in Touch when the gesture ends, in Latch when the transport stops. Each pass is one undo step.
    bool writeAutomation(const model::TrackId& id, const std::string& param, double value);
    // The value the param plays now: the value being written, or the lane's value at the playhead.
    [[nodiscard]] std::optional<double> automatedValue(const model::Track& track, std::string_view param) const;

    // The metronome plays from these settings once the next flush publishes them.
    void setMetronome(const engine::Metronome& metronome);
    // Playback and exports read the pan law from the registry; a new law reaches playback at the next flush.
    void panLawChanged(engine::PanLaw law);
    // MIDI chase and clock while stopped reach the engine at the next flush.
    void midiSyncChanged(bool chase, bool clockWhileStopped);

    // Returns how many finished takes it added, one undo step each.
    int collectTakes();
    // The track's take in progress, or null while it records none.
    [[nodiscard]] const TakePreview* takePreview(const model::TrackId& id) const;

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
    void syncChanged();
    void projectChanged();
    void devicesChanged();
    void trackErrorsChanged();
    void healthChanged();
    // The folder, the name or the edited flag changed.
    void fileChanged();
    // The track's take in progress grew, started or ended.
    void takePreviewChanged(const QString& trackId);
    // A take in progress wrote audio with these finest peaks, emitted before the preview that shows them.
    void takePeaks(const std::filesystem::path& file, const emerick::app::Peaks& peaks);
    // An audio take landed, so its peaks now come from the finished file.
    void audioTakeLanded(const std::filesystem::path& file);

private:
    struct AutomationPass {
        model::TrackId track;
        std::string param;
        core::Ticks start = 0;
        std::vector<model::AutomationPoint> points;
        double lastValue = 0.0;
    };

    void releaseAutomation();
    // Writes the passes into their lanes as one undo step and ends them.
    void commitPasses(std::vector<AutomationPass> passes, core::Ticks end);
    void applyPasses(engine::Session& session) const;
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
    [[nodiscard]] std::vector<const model::Track*> playingTracks() const;
    void sendTrackMidi(const model::Track& track, const std::vector<MidiMessage>& messages);
    void chaseTrackTypes(core::Ticks at);
    void alignStart();
    void publish();
    void updateTrackErrors();
    void keepWarmWhileActive();
    void logStreamChanges();
    void watchStream();
    void poll();
    void readLevels();
    void addTake(const engine::RecordedTake& take);
    void updatePreviews();
    void updatePreview(const std::string& trackId, engine::TakeProgress progress);
    void endPreview(const std::string& trackId);
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
    bool chaseMidi_ = true;
    bool clockWhileStopped_ = false;
    std::map<std::string, SyncStatus> syncStatus_;
    QString startReason_;
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
    std::vector<AutomationPass> passes_;
    std::map<std::string, TakePreview> previews_;
    QTimer timer_;
};

}  // namespace emerick::app
