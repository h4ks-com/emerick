#pragma once

#include <memory>
#include <string>
#include <vector>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/engine/plugin.hpp"
#include "emerick/model/document.hpp"

namespace emerick::app {

inline constexpr const char* kInsertCategory = "Tracks / Inserts";

struct InsertRef {
    model::TrackId track;
    model::InsertId insert;
};

// The insert the params name by their insert param, or by the insert id an automation param starts with.
InsertRef insertParam(const ProjectController& controller, const Json& params, const std::string& key = "insert");
const model::Insert& insertOf(const ProjectController& controller, const InsertRef& ref);
// The insert as state.get shows it, with a plugin insert's params read from its live plugin.
Json insertResult(const ProjectController& controller, const InsertRef& ref);
// The live plugin of a plugin insert, or an AppError naming why it is not loaded.
std::shared_ptr<engine::Plugin> livePlugin(ProjectController& controller, const model::Insert& insert);
Json insertSchema(Json properties, const std::vector<std::string>& required);

// insert.state, insert.setState and insert.copyState, which read and write a plugin insert's saved state.
void registerPluginStateActions(Registry& registry, ProjectController& controller);

}  // namespace emerick::app
