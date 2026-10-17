#pragma once

#include <functional>
#include <string>
#include <vector>

#include "emerick/app/lua_host.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/ui_api.hpp"

namespace emerick::app {

// A result with an "error" field is a tool error.
struct McpTool {
    std::string name;
    std::string description;
    Json inputSchema;
    std::function<Json(const Json& arguments)> call;
};

// Pass no Lua host to leave lua.eval out.
std::vector<McpTool> mcpTools(Registry& registry, const UiApi& ui, LuaHost* lua);

}  // namespace emerick::app
