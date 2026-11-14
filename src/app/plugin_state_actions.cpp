#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "emerick/app/insert_actions.hpp"
#include "emerick/app/params.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/core/base64.hpp"

namespace emerick::app {

namespace {

const model::PluginSlot& pluginOf(const model::Insert& insert) {
    if (!insert.plugin) {
        throw AppError("insert " + insert.id.value +
                       " is a built-in effect, which keeps its settings in params; set them with insert.setParam");
    }
    return *insert.plugin;
}

void requirePlugin(const model::Insert& insert, const std::string& pluginId) {
    const model::PluginSlot& slot = pluginOf(insert);
    if (slot.pluginId != pluginId) {
        throw AppError("insert " + insert.id.value + " plays " + slot.name + " (" + slot.pluginId +
                       "), and a plugin only loads its own state, not one from " + pluginId);
    }
}

// The state the live plugin holds now, or the saved one while the plugin is missing.
std::vector<std::uint8_t> currentState(ProjectController& controller, const model::Insert& insert) {
    syncInstruments(controller);
    if (const auto live = controller.instruments().find(insert.id)) {
        if (auto state = live->saveState()) {
            return *std::move(state);
        }
    }
    return pluginOf(insert).state;
}

Json setState(ProjectController& controller, const InsertRef& ref, const std::vector<std::uint8_t>& state) {
    const model::Insert& insert = insertOf(controller, ref);
    const std::string name = pluginOf(insert).name;
    livePlugin(controller, insert);
    controller.instruments().loadState(ref.insert, state);
    editTrack(controller, ref.track, "set " + name + " state", [&](model::Track& edited) {
        std::ranges::find(edited.inserts, ref.insert, &model::Insert::id)->plugin->state = state;
    });
    Json result = insertResult(controller, ref);
    result["message"] = name + " loaded the state";
    return result;
}

}  // namespace

void registerPluginStateActions(Registry& registry, ProjectController& controller) {
    registry.addAction(
        {.id = "insert.state",
         .title = "Read insert state",
         .description = "Returns a plugin insert's saved state as base64 with its plugin, which holds settings its "
                        "params leave out, such as a loaded model or impulse response. A built-in effect returns its "
                        "params only.",
         .aliases = {"plugin state", "get plugin preset"},
         .category = kInsertCategory,
         .schema = insertSchema(Json::object(), {"insert"}),
         .run = [&controller](const Json& params) {
             const InsertRef ref = insertParam(controller, params);
             const model::Insert& insert = insertOf(controller, ref);
             Json result = insertResult(controller, ref);
             if (insert.plugin) {
                 result["state"] = core::toBase64(currentState(controller, insert));
             }
             return result;
         }});
    registry.addAction(
        {.id = "insert.setState",
         .title = "Set insert state",
         .description = "Loads a state from insert.state into a plugin insert of the same plugin as one undo step.",
         .aliases = {"load plugin state", "set plugin preset"},
         .category = kInsertCategory,
         .schema = insertSchema({{"plugin", {{"type", "string"}, {"description", "the plugin id the state is from"}}},
                                 {"state", {{"type", "string"}, {"description", "base64"}}}},
                                {"insert", "plugin", "state"}),
         .run = [&controller](const Json& params) {
             const InsertRef ref = insertParam(controller, params);
             requirePlugin(insertOf(controller, ref), param<std::string>(params, "plugin"));
             const auto state = core::fromBase64(param<std::string>(params, "state"));
             if (!state || state->empty()) {
                 throw AppError("state takes the base64 text insert.state returns");
             }
             return setState(controller, ref, *state);
         }});
    registry.addAction(
        {.id = "insert.copyState",
         .title = "Copy insert state",
         .description =
             "Loads the state of the from insert into the insert, both of the same plugin, as one undo step.",
         .aliases = {"copy plugin settings"},
         .category = kInsertCategory,
         .schema = insertSchema({{"from", {{"type", "string"}}}}, {"insert", "from"}),
         .run = [&controller](const Json& params) {
             const InsertRef from = insertParam(controller, params, "from");
             const InsertRef to = insertParam(controller, params);
             const model::Insert& source = insertOf(controller, from);
             requirePlugin(insertOf(controller, to), pluginOf(source).pluginId);
             return setState(controller, to, currentState(controller, source));
         }});
}

}  // namespace emerick::app
