#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

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
    // Receives each printed line as it is printed, while set.
    void setPrintSink(std::function<void(const std::string& line)> sink);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace emerick::app
