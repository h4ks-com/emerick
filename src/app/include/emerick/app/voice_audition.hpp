#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

inline constexpr const char* kVoicesAuditionAction = "voices.audition";

// What voices.audition renders: the track's instrument as it plays now, with one stepped param set to a voice.
struct Audition {
    std::string trackId;
    model::PluginSlot slot;
    std::uint32_t param = 0;
    double value = 0;
    std::string voice;
    std::vector<int> notes;
    double seconds = 1;
    int velocity = 100;
    double sampleRate = 48000;
    std::optional<std::filesystem::path> path;
    bool play = false;
};

struct AuditionResult {
    std::vector<std::vector<float>> channels;
    Json json;
};

// UI thread: checks voices.audition's params against the track's live instrument.
Audition prepareAudition(const Registry& registry, ProjectController& controller, const Json& params);
// Any one thread, which becomes the temporary instrument's main thread. Returns no channels once stop is requested.
AuditionResult renderAudition(const Audition& audition, const std::stop_token& stop = {});

void registerAuditionAction(Registry& registry, ProjectController& controller);

// Runs voices.audition for the voice picker and plays the render on the track. A worker thread creates, renders and
// destroys the temporary instrument, so the window keeps drawing while a voice loads.
class VoicePreview : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString busyTrack READ busyTrack NOTIFY busyTrackChanged)

public:
    VoicePreview(const Registry& registry, ProjectController& controller, QObject* parent = nullptr);

    [[nodiscard]] QString busyTrack() const { return busyTrack_; }
    // Does nothing while another preview renders; a failure raises a notice.
    Q_INVOKABLE void play(const QString& track, int param, const QString& voice);

signals:
    void busyTrackChanged();

private:
    void finish(const QString& track, const AuditionResult& result, const QString& error);

    const Registry& registry_;
    ProjectController& controller_;
    QString busyTrack_;
    // Declared last, so it joins before the members its thread reads go away.
    std::jthread worker_;
};

}  // namespace emerick::app
