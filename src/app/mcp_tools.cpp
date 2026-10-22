#include "emerick/app/mcp_tools.hpp"

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

// An empty type accepts any JSON value.
Json objectSchema(std::initializer_list<std::pair<const char*, const char*>> properties,
                  const std::vector<std::string>& required = {}) {
    Json schema{{"type", "object"}, {"properties", Json::object()}};
    for (const auto& [name, type] : properties) {
        schema["properties"][name] = std::string_view{type}.empty() ? Json::object() : Json{{"type", type}};
    }
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

std::string text(const Json& arguments, const char* key) {
    return optionalParam<std::string>(arguments, key).value_or("");
}

constexpr int kConsoleLogLines = 100;

}  // namespace

std::vector<McpTool> mcpTools(Registry& registry, const UiApi& ui, LuaHost* lua, const Console* console) {
    const auto allowed = [&registry, lua](const std::string& id) {
        const Action* entry = registry.action(id);
        if (lua == nullptr && entry != nullptr && entry->runsLua) {
            throw AppError(id + " runs Lua, which this server leaves out");
        }
        return id;
    };
    std::vector<McpTool> tools{
        {"ui.tree", "Read the live UI tree of every window, plus registry entries whose UI is not revealed yet.",
         objectSchema({{"namedOnly", "boolean"}, {"root", "string"}}),
         [&ui](const Json& arguments) {
             return ui.tree(optionalParam<bool>(arguments, "namedOnly").value_or(false), text(arguments, "root"));
         }},
        {"ui.find", "Fuzzy search registry actions and settings, and find live UI items by objectName or text.",
         objectSchema({{"query", "string"}}, {"query"}),
         [&ui](const Json& arguments) { return ui.find(param<std::string>(arguments, "query")); }},
        {"ui.reveal", "Open the UI path to a location, action or setting id and return the item's geometry.",
         objectSchema({{"id", "string"}}, {"id"}),
         [&ui](const Json& arguments) { return ui.reveal(param<std::string>(arguments, "id")); }},
        {"ui.invoke", "Run an action by id, or click the UI item with that objectName.",
         objectSchema({{"id", "string"}}, {"id"}),
         [&ui, allowed](const Json& arguments) { return ui.invoke(allowed(param<std::string>(arguments, "id"))); }},
        {"ui.set", "Set a setting by id, or a property of the UI item with that objectName.",
         objectSchema({{"id", "string"}, {"property", "string"}, {"value", ""}}, {"id", "value"}),
         [&ui](const Json& arguments) {
             return ui.set(param<std::string>(arguments, "id"), param<Json>(arguments, "value"),
                           optionalParam<std::string>(arguments, "property"));
         }},
        {"actions.list", "List actions, ranked by an optional fuzzy query.", objectSchema({{"query", "string"}}),
         [&registry](const Json& arguments) {
             return Json{{"actions", registry.describeActions(text(arguments, "query"))}};
         }},
        {"actions.run", "Run an action by id with optional JSON params.",
         objectSchema({{"id", "string"}, {"params", "object"}}, {"id"}),
         [&registry, allowed](const Json& arguments) {
             return Json{{"result", registry.run(allowed(param<std::string>(arguments, "id")),
                                                 optionalParam<Json>(arguments, "params").value_or(Json::object()),
                                                 ActionSource::Mcp)}};
         }},
        {"actions.batch",
         "Run up to 1000 {id, params} calls in order, each as its own undo step, and return each call's result or "
         "error. The batch stops at the first error unless keepGoing is true.",
         objectSchema({{"calls", "array"}, {"keepGoing", "boolean"}}, {"calls"}),
         [&registry, allowed](const Json& arguments) {
             const auto calls = param<Json>(arguments, "calls");
             if (calls.is_array() && calls.size() <= kMaxBatchCalls) {
                 for (const Json& call : calls) {
                     allowed(param<std::string>(call, "id"));
                 }
             }
             return registry.runBatch(calls, optionalParam<bool>(arguments, "keepGoing").value_or(false),
                                      ActionSource::Mcp);
         }},
        {"settings.get", "Read one setting by id, or every setting.", objectSchema({{"id", "string"}}),
         [&registry](const Json& arguments) {
             if (const auto id = optionalParam<std::string>(arguments, "id")) {
                 return Json{{"setting", registry.describeSetting(*id)}};
             }
             return Json{{"settings", registry.describeSettings()}};
         }},
        {"settings.set", "Set a setting by id.", objectSchema({{"id", "string"}, {"value", ""}}, {"id", "value"}),
         [&registry](const Json& arguments) {
             const auto id = param<std::string>(arguments, "id");
             registry.setSetting(id, param<Json>(arguments, "value"));
             return Json{{"setting", registry.describeSetting(id)}};
         }},
        {"state.get", "Read one state provider by name, or all of them.", objectSchema({{"name", "string"}}),
         [&registry](const Json& arguments) {
             const auto name = optionalParam<std::string>(arguments, "name");
             return name ? Json{{*name, registry.state(*name)}} : Json::object();
         }},
        {"screenshot", "Save a PNG of the main window to path, or to a temporary file.",
         objectSchema({{"path", "string"}}),
         [&registry](const Json& arguments) {
             const std::string path = text(arguments, "path");
             return UiApi::screenshot(path.empty() ? path : registry.checkedPath(path, PathAccess::Write).string());
         }},
    };
    if (lua != nullptr) {
        tools.insert(tools.end() - 1,
                     {"lua.eval",
                      "Run Lua 5.4 with the emerick table: run, actions, setting, state and ui.find/reveal/set/invoke.",
                      objectSchema({{"code", "string"}}, {"code"}),
                      [lua](const Json& arguments) { return lua->eval(param<std::string>(arguments, "code")); }});
    }
    if (console != nullptr) {
        tools.insert(tools.end() - 1,
                     {"console.log",
                      "Read the console's recent lines: the action log as Lua calls, and console input, output and "
                      "errors.",
                      objectSchema({{"lines", "integer"}}), [console](const Json& arguments) {
                          const int lines = optionalParam<int>(arguments, "lines").value_or(kConsoleLogLines);
                          return Json{{"lines", console->recent(static_cast<std::size_t>(std::max(lines, 0)))}};
                      }});
    }
    return tools;
}

}  // namespace emerick::app
