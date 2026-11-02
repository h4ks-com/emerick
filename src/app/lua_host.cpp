#include "emerick/app/lua_host.hpp"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QString>
#include <QTimer>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#include "emerick/app/lua_helpers.hpp"
#include "emerick/app/lua_text.hpp"

namespace emerick::app {

namespace {

constexpr int kMaxDepth = 64;
constexpr int kStackPerLevel = 8;
constexpr int kHookInstructions = 10'000;
constexpr int kInstructionLimit = 100'000'000;
constexpr std::array kRemovedGlobals{"dofile", "loadfile", "load", "require"};
// Lua patterns backtrack without a bound and run in C, where the instruction hook never fires, so scripts get only
// plain string.find.
struct Budget {
    std::size_t used = 0;
    std::size_t limit = Limits{}.luaMemoryBytes;
    int instructions = 0;
    std::optional<std::chrono::milliseconds> timeout;
    std::chrono::steady_clock::time_point deadline;
    std::string stopped;
};

Budget& budgetOf(lua_State* state) {
    void* budget = nullptr;
    lua_getallocf(state, &budget);
    return *static_cast<Budget*>(budget);
}

// We follow lua_Alloc's contract, which is realloc and free with a byte budget on top.
void* limitedAlloc(void* budgetPointer, void* block, std::size_t oldSize, std::size_t newSize) {
    auto* budget = static_cast<Budget*>(budgetPointer);
    const std::size_t held = block == nullptr ? 0 : oldSize;
    if (newSize == 0) {
        std::free(block);  // NOLINT(cppcoreguidelines-no-malloc)
        budget->used -= held;
        return nullptr;
    }
    if (newSize > held && budget->used + (newSize - held) > budget->limit) {
        return nullptr;
    }
    void* moved = std::realloc(block, newSize);  // NOLINT(cppcoreguidelines-no-malloc)
    if (moved != nullptr) {
        budget->used = budget->used - held + newSize;
    }
    return moved;
}

// A pcall in the script catches one error, so after a limit we raise again on every instruction until it ends.
void stopRunawayScript(lua_State* state, lua_Debug* /*debug*/) {
    Budget& budget = budgetOf(state);
    if (budget.stopped.empty()) {
        budget.instructions += kHookInstructions;
        if (budget.instructions >= kInstructionLimit) {
            budget.stopped = std::format("script ran over {} instructions", kInstructionLimit);
        } else if (budget.timeout && std::chrono::steady_clock::now() > budget.deadline) {
            budget.stopped = std::format("script ran longer than {} ms", budget.timeout->count());
        } else {
            return;
        }
        lua_sethook(state, stopRunawayScript, LUA_MASKCOUNT, 1);
    }
    lua_pushstring(state, budget.stopped.c_str());
    lua_error(state);
}

class RunBudget {
public:
    RunBudget(lua_State* state, const Limits& limits) : state_(state) {
        Budget& budget = budgetOf(state_);
        budget.limit = limits.luaMemoryBytes;
        budget.instructions = 0;
        budget.timeout = limits.luaTimeout;
        budget.deadline = std::chrono::steady_clock::now() + limits.luaTimeout.value_or(std::chrono::milliseconds{});
        budget.stopped.clear();
        lua_sethook(state_, stopRunawayScript, LUA_MASKCOUNT, kHookInstructions);
    }
    ~RunBudget() { lua_sethook(state_, nullptr, 0, 0); }
    RunBudget(const RunBudget&) = delete;
    RunBudget& operator=(const RunBudget&) = delete;
    RunBudget(RunBudget&&) = delete;
    RunBudget& operator=(RunBudget&&) = delete;

private:
    lua_State* state_;
};

// The time a script waits on the event loop counts toward no time limit.
class PausedDeadline {
public:
    explicit PausedDeadline(lua_State* state) : budget_(budgetOf(state)) {}
    ~PausedDeadline() { budget_.deadline += std::chrono::steady_clock::now() - start_; }
    PausedDeadline(const PausedDeadline&) = delete;
    PausedDeadline& operator=(const PausedDeadline&) = delete;
    PausedDeadline(PausedDeadline&&) = delete;
    PausedDeadline& operator=(PausedDeadline&&) = delete;

private:
    Budget& budget_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

sol::object toLua(sol::state_view lua, const Json& value) {
    switch (value.type()) {
        case Json::value_t::boolean:
            return sol::make_object(lua, value.get<bool>());
        case Json::value_t::number_integer:
        case Json::value_t::number_unsigned:
            return sol::make_object(lua, value.get<std::int64_t>());
        case Json::value_t::number_float:
            return sol::make_object(lua, value.get<double>());
        case Json::value_t::string:
            return sol::make_object(lua, value.get<std::string>());
        case Json::value_t::array: {
            sol::table table = lua.create_table(static_cast<int>(value.size()), 0);
            for (std::size_t i = 0; i < value.size(); ++i) {
                table[i + 1] = toLua(lua, value[i]);
            }
            return table;
        }
        case Json::value_t::object: {
            sol::table table = lua.create_table(0, static_cast<int>(value.size()));
            for (const auto& [key, item] : value.items()) {
                table[key] = toLua(lua, item);
            }
            return table;
        }
        default:
            return sol::make_object(lua, sol::lua_nil);
    }
}

std::size_t rawLength(const sol::table& table) {
    table.push();
    const auto length = static_cast<std::size_t>(lua_rawlen(table.lua_state(), -1));
    lua_pop(table.lua_state(), 1);
    return length;
}

// Lua strings are any bytes, and JSON text must be UTF-8, so we replace the bytes that are not.
std::string utf8Text(const sol::object& value) {
    const auto bytes = value.as<std::string_view>();
    return QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size())).toStdString();
}

// Only raw access, so no metamethod runs while we convert a result.
Json fromLua(const sol::object& value, int depth = 0) {
    if (depth > kMaxDepth) {
        throw AppError("Lua table nests deeper than 64 levels or contains a cycle");
    }
    switch (value.get_type()) {
        case sol::type::boolean:
            return value.as<bool>();
        case sol::type::number: {
            value.push();
            const bool integer = lua_isinteger(value.lua_state(), -1) != 0;
            lua_pop(value.lua_state(), 1);
            return integer ? Json(value.as<std::int64_t>()) : Json(value.as<double>());
        }
        case sol::type::string:
            return utf8Text(value);
        case sol::type::table: {
            // Each level holds its table, key and value on the Lua stack, which Lua never grows for us.
            if (lua_checkstack(value.lua_state(), kStackPerLevel) == 0) {
                throw AppError("Lua table nests too deep to convert");
            }
            const auto table = value.as<sol::table>();
            const std::size_t length = rawLength(table);
            std::size_t count = 0;
            table.for_each([&count](const sol::object&, const sol::object&) { ++count; });
            if (length > 0 && length == count) {
                Json array = Json::array();
                for (std::size_t i = 1; i <= length; ++i) {
                    array.push_back(fromLua(table.raw_get<sol::object>(i), depth + 1));
                }
                return array;
            }
            Json object = Json::object();
            table.for_each([&](const sol::object& key, const sol::object& item) {
                const std::string name =
                    key.get_type() == sol::type::string ? utf8Text(key) : fromLua(key, depth + 1).dump();
                object[name] = fromLua(item, depth + 1);
            });
            return object;
        }
        default:
            return nullptr;
    }
}

Json fromLuaArgument(const sol::optional<sol::object>& value) {
    return value && value->valid() ? fromLua(*value) : Json::object();
}

}  // namespace

namespace {

// An error thrown by a C++ function names the script line that called it, as a Lua error does.
int errorWithLocation(lua_State* state, sol::optional<const std::exception&> /*error*/, sol::string_view description) {
    luaL_where(state, 1);
    lua_pushlstring(state, description.data(), description.size());
    lua_concat(state, 2);
    return 1;
}

sol::object memberFunction(sol::table table, std::string_view name) {
    for (const auto part : name | std::views::split('.')) {
        const sol::object item = table.raw_get<sol::object>(std::string_view{part});
        if (item.get_type() != sol::type::table) {
            return item;
        }
        table = item.as<sol::table>();
    }
    return table;
}

bool sameValue(const sol::object& a, const sol::object& b) {
    a.push();
    b.push();
    const bool same = lua_rawequal(a.lua_state(), -1, -2) != 0;
    lua_pop(a.lua_state(), 2);
    return same;
}

constexpr std::array kLuaMembers{
    LuaMember{"run", "run(id, params?)", "Run an action by id with a params table and return its result."},
    LuaMember{"batch", "batch(calls, keepGoing?)",
              "Run up to 1000 {id = ..., params = ...} calls in order and return each result or error; stop at the "
              "first error unless keepGoing."},
    LuaMember{"actions", "actions(query?)", "List actions with their params, ranked by an optional fuzzy query."},
    LuaMember{"setting", "setting(id, value?)", "Read a setting, or set it when value is given."},
    LuaMember{"state", "state(name)", "Read a state provider: project, transport, devices, nav, app or limits."},
    LuaMember{"project", "project()", "Read the project: tracks with their ids, names and clips, and the tempo."},
    LuaMember{"ui.find", "ui.find(query)", "Find actions, settings and live UI items by name."},
    LuaMember{"ui.reveal", "ui.reveal(id)", "Open the UI path to an action, setting or location."},
    LuaMember{"ui.set", "ui.set(id, value, property?)", "Set a setting, or a property of the UI item with that id."},
    LuaMember{"ui.invoke", "ui.invoke(id)", "Run an action, or click the UI item with that id."},
    LuaMember{"wait", "wait(seconds, done?)",
              "Let background work run for up to seconds, or until done() returns true, and return whether it did; "
              "call it from a script started with --script."},
    LuaMember{"waitReady", "waitReady(seconds?)",
              "Wait until every instrument has loaded its sounds, for up to seconds (default 60), and raise an error "
              "after that."},
    LuaMember{"bus", "bus(name, inserts?)",
              "Add a bus with an insert per {type = params} table of the inserts list, in order, such as "
              "emerick.bus('Verb', {{reverb = {mix = 1}}}), and return its id and the insert ids."},
    LuaMember{"automate", "automate(track, param, points)",
              "Add a point to the lane of param on the track, by id or name, for each point in points, and return the "
              "point ids. A point is {bar, beat, value, curve?}, {bar = b, beat = b, value = v, curve = c} or {at = "
              "position, value = v, curve = c}, where beat may be fractional, at is ticks or a bar.beat.tick string "
              "and curve is linear, hold or smooth."},
};

constexpr double kMaxWaitSeconds = 86'400;
constexpr std::chrono::milliseconds kWaitSlice{50};

bool holds(const sol::protected_function& done) {
    const sol::protected_function_result call = done();
    if (!call.valid()) {
        throw sol::error(call.get<sol::error>());
    }
    return call.get<bool>();
}

bool waitFor(lua_State* state, double seconds, const sol::optional<sol::protected_function>& done) {
    const QDeadlineTimer deadline(std::chrono::milliseconds{std::llround(seconds * 1000)});
    while (!(done && holds(*done))) {
        if (deadline.hasExpired()) {
            return false;
        }
        const PausedDeadline paused(state);
        QEventLoop loop;
        QTimer::singleShot(
            std::min(std::chrono::duration_cast<std::chrono::milliseconds>(deadline.remainingTimeAsDuration()),
                     kWaitSlice),
            &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

}  // namespace

std::span<const LuaMember> luaMembers() { return kLuaMembers; }

struct LuaHost::State {
    Budget budget;
    sol::state lua{sol::default_at_panic, limitedAlloc, &budget};
    std::string output;
    std::function<void(const std::string&)> printSink;
    int depth = 0;
    bool canWait = false;

    void print(const std::string& line) {
        output += line + '\n';
        if (printSink) {
            printSink(line);
        }
    }

    sol::load_result load(const LuaChunk& chunk) {
        if (chunk.expression) {
            sol::load_result expression = lua.load("return " + chunk.code, chunk.name, sol::load_mode::text);
            if (expression.valid()) {
                return expression;
            }
        }
        return lua.load(chunk.code, chunk.name, sol::load_mode::text);
    }

    Json run(const LuaChunk& code) {
        if (code.commandLine) {
            sol::table arg = lua.create_table(static_cast<int>(code.commandLine->size()), 0);
            for (std::size_t i = 0; i < code.commandLine->size(); ++i) {
                arg[i] = (*code.commandLine)[i];
            }
            lua["arg"] = arg;
        }
        const sol::load_result chunk = load(code);
        if (!chunk.valid()) {
            return {{"error", chunk.get<sol::error>().what()}};
        }
        const sol::protected_function_result call = chunk.get<sol::protected_function>()();
        if (!call.valid()) {
            return {{"error", call.get<sol::error>().what()}};
        }
        return {{"ok", true}, {"result", call.return_count() > 0 ? fromLua(call.get<sol::object>(0)) : Json(nullptr)}};
    }
};

LuaHost::LuaHost(Registry& registry, const UiApi& ui) : registry_(&registry), state_(std::make_unique<State>()) {
    sol::state& lua = state_->lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8);
    for (const char* name : kRemovedGlobals) {
        lua[name] = sol::lua_nil;
    }
    lua.set_exception_handler(errorWithLocation);

    lua["print"] = [this](sol::variadic_args args, sol::this_state current) {
        const sol::protected_function toString = sol::state_view{current}.get<sol::protected_function>("tostring");
        std::string line;
        for (const auto& arg : args) {
            const sol::protected_function_result text = toString(arg);
            if (!text.valid()) {
                throw sol::error(text.get<sol::error>());
            }
            if (!line.empty()) {
                line += '\t';
            }
            line += text.get<std::string>();
        }
        state_->print(line);
    };

    sol::table emerick = lua.create_named_table("emerick");
    emerick["run"] = [&registry](const std::string& id, const sol::optional<sol::object>& params,
                                 sol::this_state current) {
        return toLua(current, registry.run(id, fromLuaArgument(params), ActionSource::Lua));
    };
    emerick["batch"] = [&registry](const sol::table& calls, const sol::optional<bool>& keepGoing,
                                   sol::this_state current) {
        const Json given = fromLua(calls);
        return toLua(current, registry.runBatch(given.empty() ? Json::array() : given, keepGoing.value_or(false),
                                                ActionSource::Lua));
    };
    emerick["actions"] = [&registry](const sol::optional<std::string>& query, sol::this_state current) {
        return toLua(current, registry.describeActions(query.value_or("")));
    };
    emerick["setting"] = [&registry](const std::string& id, const sol::optional<sol::object>& value,
                                     sol::this_state current) {
        if (value && value->valid()) {
            return toLua(current, registry.setSetting(id, fromLua(*value)));
        }
        return toLua(current, registry.settingValue(id));
    };
    emerick["state"] = [&registry](const std::string& name, sol::this_state current) {
        return toLua(current, registry.state(name));
    };
    emerick["project"] = [&registry](sol::this_state current) { return toLua(current, registry.state("project")); };
    emerick["waitReady"] = [&registry](const sol::optional<double>& seconds) {
        registry.run("project.waitReady", seconds ? Json{{"timeout", *seconds}} : Json::object(), ActionSource::Lua);
    };
    emerick["bus"] = [&registry](const std::string& name, const sol::optional<sol::object>& inserts,
                                 sol::this_state current) {
        const Json result = addBus(registry, name, fromLuaArgument(inserts));
        return std::make_tuple(toLua(current, result.at("bus")), toLua(current, result.at("inserts")));
    };
    emerick["automate"] = [&registry](const std::string& track, const std::string& param, const sol::table& points,
                                      sol::this_state current) {
        return toLua(current, automate(registry, track, param, fromLua(points)).at("points"));
    };

    // An MCP request or a console line that ran the event loop could start another request inside its own, so we
    // let only a command line script wait, and never inside its own wait.
    emerick["wait"] = [this](double seconds, const sol::optional<sol::protected_function>& done,
                             sol::this_state current) {
        if (!state_->canWait) {
            throw AppError("emerick.wait runs only in a script started with --script");
        }
        if (std::isnan(seconds) || seconds < 0 || seconds > kMaxWaitSeconds) {
            throw AppError("emerick.wait takes from 0 to 86400 seconds");
        }
        state_->canWait = false;
        const auto restore = qScopeGuard([this] { state_->canWait = true; });
        return waitFor(current, seconds, done);
    };

    sol::table uiTable = emerick.create_named("ui");
    uiTable["find"] = [&ui](const std::string& query, sol::this_state current) {
        return toLua(current, ui.find(query));
    };
    uiTable["reveal"] = [&ui](const std::string& id, sol::this_state current) { return toLua(current, ui.reveal(id)); };
    uiTable["set"] = [&ui](const std::string& id, const sol::object& value, const sol::optional<std::string>& property,
                           sol::this_state current) {
        return toLua(current, ui.set(id, fromLua(value), property ? std::optional{*property} : std::nullopt));
    };
    uiTable["invoke"] = [&ui](const std::string& id, sol::this_state current) {
        return toLua(current, ui.invoke(id, ActionSource::Lua));
    };

    lua["help"] = [this, &registry](const sol::optional<sol::object>& subject, sol::this_state current) {
        std::string name;
        if (subject && subject->is<std::string>()) {
            name = subject->as<std::string>();
        } else if (subject && subject->get_type() == sol::type::function) {
            const sol::table emerickTable = sol::state_view{current}["emerick"];
            const auto* const found = std::ranges::find_if(kLuaMembers, [&](const LuaMember& entry) {
                return sameValue(memberFunction(emerickTable, entry.name), *subject);
            });
            if (found == kLuaMembers.end()) {
                throw AppError("help takes an action id, a setting id or an emerick function");
            }
            name = found->name;
        }
        state_->print(helpText(registry, name));
    };
}

LuaHost::~LuaHost() = default;

std::function<void(const std::string&)> LuaHost::setPrintSink(std::function<void(const std::string&)> sink) {
    return std::exchange(state_->printSink, std::move(sink));
}

Json LuaHost::eval(const LuaChunk& chunk) {
    State& state = *state_;
    if (state.depth == 0) {
        state.output.clear();
    }
    const std::size_t mark = state.output.size();
    Json result{{"ok", false}};
    {
        const auto restore = qScopeGuard([&state, outer = state.canWait] { state.canWait = outer; });
        state.canWait = chunk.commandLine.has_value();
        std::optional<RunBudget> budget;
        if (state.depth == 0) {
            budget.emplace(state.lua.lua_state(), registry_->limits());
        }
        ++state.depth;
        try {
            result.update(registry_->runFrom(ActionSource::Lua, [&] { return state.run(chunk); }));
        } catch (const std::exception& error) {
            result["error"] = error.what();
        }
        --state.depth;
    }
    result["output"] = state.output.substr(mark);
    return result;
}

std::string errorOf(const Json& result) {
    const auto error = result.at("error").get<std::string>();
    return error.substr(0, error.find("\nstack traceback:"));
}

int runCommandLineScript(LuaHost& lua, const std::filesystem::path& script, std::span<const std::string> args,
                         std::ostream& out, std::ostream& err) {
    std::error_code error;
    std::ifstream file(script);
    if (!std::filesystem::is_regular_file(script, error) || !file) {
        err << "cannot read script " << script.string() << '\n';
        return 2;
    }
    std::vector<std::string> commandLine{script.string()};
    commandLine.insert(commandLine.end(), args.begin(), args.end());
    auto outer = lua.setPrintSink([&out](const std::string& line) { out << line << '\n' << std::flush; });
    const Json result = lua.eval({.code = {std::istreambuf_iterator<char>(file), {}},
                                  .name = "=" + script.filename().string(),
                                  .commandLine = std::move(commandLine)});
    lua.setPrintSink(std::move(outer));
    if (result.at("ok") != true) {
        err << errorOf(result) << '\n';
        return 1;
    }
    return 0;
}

}  // namespace emerick::app
