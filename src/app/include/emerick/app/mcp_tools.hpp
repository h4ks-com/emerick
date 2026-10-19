#pragma once

#include <functional>
#include <string>
#include <vector>

#include "emerick/app/console.hpp"
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

// Pass no Lua host to leave lua.eval out and refuse the actions that run Lua; pass no console to leave console.log
// out.
std::vector<McpTool> mcpTools(Registry& registry, const UiApi& ui, LuaHost* lua, const Console* console = nullptr);

}  // namespace emerick::app
