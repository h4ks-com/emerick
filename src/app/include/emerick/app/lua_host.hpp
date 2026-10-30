#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/app/ui_api.hpp"

namespace emerick::app {

// One function of the emerick table, as help() and the console's completion list show it.
struct LuaMember {
    std::string_view name;
    std::string_view signature;
    std::string_view summary;
};

std::span<const LuaMember> luaMembers();

struct LuaChunk {
    std::string code;
    // Errors read "name:line: message".
    std::string name = "eval";
    // Code that reads as an expression returns its value, as in the lua command line.
    bool expression = false;
    // The script and its arguments, as the global arg table of the lua command line holds them. Only a chunk that
    // has them may call emerick.wait.
    std::optional<std::vector<std::string>> commandLine = std::nullopt;
};

class LuaHost {
public:
    LuaHost(Registry& registry, const UiApi& ui);
    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;
    LuaHost(LuaHost&&) = delete;
    LuaHost& operator=(LuaHost&&) = delete;
    ~LuaHost();

    // Returns {ok, result | error, output}, where output holds everything the script printed. A script an action
    // runs from inside another script shares its instruction budget.
    Json eval(const LuaChunk& chunk);
    Json eval(const std::string& code) { return eval(LuaChunk{.code = code}); }
    // Receives each printed line as it is printed, while set. Returns the sink it replaces.
    std::function<void(const std::string& line)> setPrintSink(std::function<void(const std::string& line)> sink);

private:
    Registry* registry_;
    struct State;
    std::unique_ptr<State> state_;
};

// The error of a failed eval as "name:line: message", without sol's traceback after it.
std::string errorOf(const Json& result);

// Runs a script file started from the command line, printing to out and reporting an error to err as
// "file:line: message". Returns the exit code: 0, 1 when the script fails, 2 when the file cannot be read.
int runCommandLineScript(LuaHost& lua, const std::filesystem::path& script, std::span<const std::string> args,
                         std::ostream& out, std::ostream& err);

}  // namespace emerick::app
