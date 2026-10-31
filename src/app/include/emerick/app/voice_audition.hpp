#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "emerick/app/background_render.hpp"
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
    // The MIDI channel, from 1, the instrument plays the param's choices on.
    int channel = 1;
    double seconds = 1;
    int velocity = 100;
    double sampleRate = 48000;
    std::optional<std::filesystem::path> path;
    bool play = false;
};

// UI thread: checks voices.audition's params against the track's live instrument.
Audition prepareAudition(const Registry& registry, ProjectController& controller, const Json& params);
// UI thread: sets up the render of the audition on a temporary copy of the instrument and its measurement.
RenderJob auditionJob(const Audition& audition);

void registerAuditionAction(Registry& registry, ProjectController& controller);

// Runs voices.audition for the voice picker in the background and plays the render on the track.
class VoicePreview : public BackgroundRender {
    Q_OBJECT

public:
    VoicePreview(const Registry& registry, ProjectController& controller, QObject* parent = nullptr);

    // Does nothing while another preview renders; a failure raises a notice.
    Q_INVOKABLE void play(const QString& track, int param, const QString& voice);

private:
    ProjectController& controller_;
};

}  // namespace emerick::app
