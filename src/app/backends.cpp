#include "emerick/app/backends.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

constexpr const char* kVirtualPort = "Virtual Port 1";

template <class Backend>
using BackendFactory = std::pair<std::string_view, std::function<std::unique_ptr<Backend>()>>;

template <class Backend>
std::unique_ptr<Backend> makeBackend(std::string_view name, const std::vector<BackendFactory<Backend>>& factories,
                                     std::string_view flag) {
    const auto found = std::ranges::find(factories, name, &BackendFactory<Backend>::first);
    if (found == factories.end()) {
        throw AppError("unknown " + std::string{flag} + " backend " + std::string{name} + ", use one of " +
                       std::string{factories.front().first} + " or " + std::string{factories.back().first});
    }
    return found->second();
}

}  // namespace

void registerVirtualMidiActions(Registry& registry, engine::VirtualMidiBackend& midi) {
    registry.addAction({.id = "midi.inject",
                        .title = "Inject MIDI into a virtual port",
                        .aliases = {"send test midi"},
                        .category = "Devices",
                        .schema = {{"required", {"port", "bytes"}},
                                   {"properties", {{"port", {{"type", "string"}}}, {"bytes", {{"type", "array"}}}}}},
                        .run = [&midi](const Json& params) {
                            const auto port = param<std::string>(params, "port");
                            const auto bytes = param<std::vector<std::uint8_t>>(params, "bytes");
                            if (bytes.empty()) {
                                throw AppError("bytes must hold one MIDI message");
                            }
                            const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch());
                            midi.inject(port, bytes, now.count());
                            return Json{{"port", port}, {"bytes", bytes}};
                        }});
    registry.addAction({.id = "midi.sent",
                        .title = "List MIDI sent to a virtual port",
                        .category = "Devices",
                        .schema = {{"required", {"port"}},
                                   {"properties", {{"port", {{"type", "string"}}}, {"clear", {{"type", "boolean"}}}}}},
                        .run = [&midi](const Json& params) {
                            const auto port = param<std::string>(params, "port");
                            Json messages = Json::array();
                            for (const engine::SentMidi& sent : midi.sent(port)) {
                                messages.push_back(sent.bytes);
                            }
                            if (optionalParam<bool>(params, "clear").value_or(false)) {
                                midi.clearSent();
                            }
                            return Json{{"port", port}, {"messages", std::move(messages)}};
                        }});
}

std::unique_ptr<engine::AudioBackend> makeAudioBackend(std::string_view name) {
    static const std::vector<BackendFactory<engine::AudioBackend>> factories{
        {"null", [] { return std::make_unique<engine::NullAudioBackend>(); }},
#ifdef __APPLE__
        {"default", [] { return std::make_unique<engine::CoreAudioBackend>(); }}};
#else
        {"default", [] { return std::make_unique<engine::MiniaudioBackend>(); }}};
#endif
    return makeBackend(name, factories, "--audio");
}

std::unique_ptr<engine::MidiBackend> makeMidiBackend(std::string_view name) {
    static const std::vector<BackendFactory<engine::MidiBackend>> factories{
        {"virtual",
         [] {
             return std::make_unique<engine::VirtualMidiBackend>(std::vector<std::string>{kVirtualPort},
                                                                 std::vector<std::string>{kVirtualPort});
         }},
        {"default", [] { return std::make_unique<engine::LibremidiBackend>(); }}};
    return makeBackend(name, factories, "--midi");
}

}  // namespace emerick::app
