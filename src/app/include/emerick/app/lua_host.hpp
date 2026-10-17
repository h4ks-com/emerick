#pragma once

#include <memory>
#include <string>

#include "emerick/app/registry.hpp"
#include "emerick/app/ui_api.hpp"

namespace emerick::app {

class LuaHost {
public:
    LuaHost(Registry& registry, const UiApi& ui);
    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;
    LuaHost(LuaHost&&) = delete;
    LuaHost& operator=(LuaHost&&) = delete;
    ~LuaHost();

    // Returns {ok, result | error, output}, where output holds everything the script printed.
    Json eval(const std::string& code);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace emerick::app
