#include "emerick/app/action_params.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string_view>

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

const engine::EffectSpec& effectType(const std::string& type) {
    const engine::EffectSpec* spec = engine::findEffect(type);
    if (spec == nullptr) {
        const std::string known = engine::kEffects | std::views::transform(&engine::EffectSpec::id) |
                                  std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
        throw AppError("unknown effect type " + type + ", use one of " + known);
    }
    return *spec;
}

const engine::EffectParam& effectParamNamed(const engine::EffectSpec& spec, const std::string& name) {
    const auto index = engine::paramIndex(spec, name);
    if (!index) {
        const std::string known = spec.params | std::views::transform(&engine::EffectParam::id) |
                                  std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>();
        throw AppError(std::string{spec.title} + " has no param " + name + ", use one of " + known);
    }
    return spec.params[*index];
}

double effectParamValue(const engine::EffectParam& described, const Json& value) {
    double number = 0.0;
    if (value.is_string() && described.kind == engine::ParamKind::choice) {
        const auto found = std::ranges::find(described.choices, value.get<std::string>());
        if (found == described.choices.end()) {
            throw AppError("param " + std::string{described.id} + " has no choice " + value.get<std::string>());
        }
        number = static_cast<double>(found - described.choices.begin());
    } else if (value.is_number()) {
        number = value.get<double>();
    } else {
        throw AppError("param " + std::string{described.id} + " takes a number, got " + value.dump());
    }
    switch (described.kind) {
        case engine::ParamKind::toggle:
            number = number != 0.0 ? 1.0 : 0.0;
            break;
        case engine::ParamKind::choice:
            number = std::round(number);
            break;
        case engine::ParamKind::number:
            break;
    }
    return std::clamp(number, static_cast<double>(described.minimum), static_cast<double>(described.maximum));
}

}  // namespace emerick::app
