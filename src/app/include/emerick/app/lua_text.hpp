#pragma once

#include <string>

#include "emerick/app/registry.hpp"

namespace emerick::app {

// A JSON value as Lua source, such as {track="Audio 1", value=true}.
std::string luaLiteral(const Json& value);
// The Lua line that runs the action, such as emerick.run("track.setMute", {track="Audio 1", value=true}).
std::string luaCall(const std::string& id, const Json& params);
// An action's params from its schema, such as "track?: string, value?: boolean"; a ? marks an optional param.
std::string paramSummary(const Json& schema);
// What help(name) prints: an emerick function, an action or a setting, or every emerick function for an empty name.
std::string helpText(const Registry& registry, const std::string& name);

}  // namespace emerick::app
