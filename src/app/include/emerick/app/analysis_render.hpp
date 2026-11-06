#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "emerick/app/background_render.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/stem_export.hpp"

namespace emerick::app {

inline constexpr const char* kMixInspectAction = "mix.inspect";

std::string loudnessMessage(double integratedLufs, double truePeakDbtp, const std::string& detail);

// UI thread: checks track.analyze's params and sets up the render of its track and its measurement, keyed by its id.
RenderJob prepareTrackRender(ProjectController& controller, const Registry& registry, const Json& params);
// UI thread: checks tracks.analyze's params and sets up one render of every analyzable track and their measurements,
// keyed by track id.
RenderJob prepareTracksRender(ProjectController& controller, const Registry& registry, const Json& params);
// UI thread: checks mix.inspect's params and sets up one render of the mix and every analyzable track, and the issues
// found in them.
RenderJob prepareMixInspection(ProjectController& controller, const Registry& registry, const Json& params);

struct ProjectRender;

// Keeps the last render of the whole project, its mix and every analyzable track measured as mix.inspect measures
// them, so tracks.analyze, mix.inspect and export.mix share one render while the project, its plugins' states, the
// files it names and the render settings stay as they were. A call with from, to or resolution renders on its own.
class ProjectRenders {
public:
    // tracks.analyze's measurement of each track, keyed by track id.
    Json analyzeTracks(ProjectController& controller, const Registry& registry, const Json& params);
    Json inspectMix(ProjectController& controller, const Registry& registry, const Json& params);
    // Writes export.mix's file and returns the levels it ends at with a target.
    std::optional<MixLevels> exportMix(ProjectController& controller, const Registry& registry,
                                       const std::filesystem::path& file, const std::optional<LoudnessTarget>& target,
                                       AudioEncoding encoding);

private:
    const ProjectRender& current(ProjectController& controller, const Registry& registry,
                                 const model::Document& document);

    std::shared_ptr<const ProjectRender> last_;
    std::shared_ptr<SourceStems> stems_;
};

}  // namespace emerick::app
