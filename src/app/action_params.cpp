#include "action_params.hpp"

#include <algorithm>
#include <stdexcept>

namespace emerick::app {

model::TrackId trackParam(const model::Project& project, const Json& params, const std::string& key) {
    return findTrackByIdOrName(project, param<std::string>(params, key));
}

model::TrackId findTrackByIdOrName(const model::Project& project, const std::string& idOrName) {
    const auto& tracks = project.document().tracks;
    if (const auto byId = std::ranges::find(tracks, idOrName, [](const model::Track& t) { return t.id.value; });
        byId != tracks.end()) {
        return byId->id;
    }
    if (const auto byName = std::ranges::find(tracks, idOrName, &model::Track::name); byName != tracks.end()) {
        return byName->id;
    }
    throw AppError("no track with id or name " + idOrName + ", read the project state for track ids");
}

model::ClipId clipParam(const model::Project& project, const Json& params, const std::string& key) {
    model::ClipId id{param<std::string>(params, key)};
    if (!project.findClip(id)) {
        throw AppError("no clip " + id.value + ", read the project state for clip ids");
    }
    return id;
}

std::optional<core::Ticks> optionalPosition(const Json& params, const std::string& key, const core::TempoMap& tempo) {
    const auto found = params.find(key);
    if (found == params.end() || found->is_null()) {
        return std::nullopt;
    }
    if (found->is_number_integer()) {
        return found->get<core::Ticks>();
    }
    if (found->is_string()) {
        try {
            return tempo.fromBarBeatTick(core::BarBeatTick::parse(found->get<std::string>()));
        } catch (const std::invalid_argument& error) {
            throw AppError("param " + key + ": " + error.what());
        }
    }
    throw AppError("param " + key + " expects ticks or a bar.beat.tick string, got " + found->dump());
}

core::Ticks positionParam(const Json& params, const std::string& key, const core::TempoMap& tempo) {
    if (const auto ticks = optionalPosition(params, key, tempo)) {
        return *ticks;
    }
    throw AppError("missing param " + key);
}

}  // namespace emerick::app
