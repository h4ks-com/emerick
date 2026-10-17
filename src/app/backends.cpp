#include "emerick/app/backends.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/registry.hpp"

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

std::unique_ptr<engine::AudioBackend> makeAudioBackend(std::string_view name) {
    static const std::vector<BackendFactory<engine::AudioBackend>> factories{
        {"null", [] { return std::make_unique<engine::NullAudioBackend>(); }},
        {"default", [] { return std::make_unique<engine::MiniaudioBackend>(); }}};
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
