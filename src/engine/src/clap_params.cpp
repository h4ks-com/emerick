// The main-thread side of a CLAP plugin's params: listing them, reading their values and labels, and setting them.

#include <array>
#include <chrono>
#include <format>
#include <ranges>
#include <thread>

#include "clap_host.hpp"

namespace emerick::engine {

namespace {

// A plugin that stopped processing without telling us leaves the queue unread, so we flush it ourselves after this.
constexpr auto kParamWait = std::chrono::milliseconds(50);

std::optional<PluginParam> paramAt(const ClapPlugin& plugin, const ClapPluginProxy& proxy, std::uint32_t index) {
    clap_param_info info{};
    if (!proxy.paramsGetInfo(index, &info) || (info.flags & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY)) != 0) {
        return std::nullopt;
    }
    PluginParam param{.id = info.id,
                      .name = std::data(info.name),
                      .module = std::data(info.module),
                      .min = info.min_value,
                      .max = info.max_value,
                      .defaultValue = info.default_value,
                      .stepped = (info.flags & CLAP_PARAM_IS_STEPPED) != 0,
                      .enumerated = (info.flags & CLAP_PARAM_IS_ENUM) != 0,
                      .choices = {}};
    if (param.stepped && param.max >= param.min && param.max - param.min < kMaxParamChoices) {
        const auto steps = static_cast<int>(param.max - param.min);
        param.choices = std::views::iota(0, steps + 1) |
                        std::views::transform([&](int step) { return plugin.paramText(param.id, param.min + step); }) |
                        std::ranges::to<std::vector>();
    }
    return param;
}

}  // namespace

void ClapPlugin::Impl::flushParams() {
    if (!proxy->canUseParams()) {
        return;
    }
    eventCount = 0;
    collectParams();
    const clap_input_events input = inputEvents();
    const clap_output_events output = outputEvents();
    if (active) {
        const AudioThreadScope audioThread;
        proxy->paramsFlush(&input, &output);
    } else {
        proxy->paramsFlush(&input, &output);
    }
    paramsApplied.fetch_add(paramEventCount, std::memory_order_release);
    eventCount = 0;
    paramEventCount = 0;
}

std::vector<PluginParam> ClapPlugin::params() const {
    const ClapPluginProxy& proxy = *impl_->proxy;
    if (!proxy.canUseParams()) {
        return {};
    }
    return std::views::iota(std::uint32_t{0}, proxy.paramsCount()) |
           std::views::transform([&](std::uint32_t index) { return paramAt(*this, proxy, index); }) |
           std::views::filter([](const std::optional<PluginParam>& param) { return param.has_value(); }) |
           std::views::transform([](const std::optional<PluginParam>& param) { return *param; }) |
           std::ranges::to<std::vector>();
}

std::optional<double> ClapPlugin::paramValue(std::uint32_t id) const {
    double value = 0.0;
    if (!impl_->proxy->canUseParams() || !impl_->proxy->paramsGetValue(id, &value)) {
        return std::nullopt;
    }
    return value;
}

std::string ClapPlugin::paramText(std::uint32_t id, double value) const {
    std::array<char, CLAP_NAME_SIZE> text{};
    if (!impl_->proxy->canUseParams() || !impl_->proxy->paramsValueToText(id, value, text.data(), text.size())) {
        return std::format("{}", value);
    }
    return text.data();
}

void ClapPlugin::setParam(std::uint32_t id, double value) {
    Impl& impl = *impl_;
    // We wait for every change before returning, so the queue holds at most this one.
    if (!impl.proxy->canUseParams() || !writeRecord(impl.paramQueue, QueuedParam{.id = id, .value = value})) {
        return;
    }
    ++impl.paramsSent;
    const auto deadline = std::chrono::steady_clock::now() + kParamWait;
    while (impl.paramsApplied.load(std::memory_order_acquire) < impl.paramsSent) {
        if (!impl.processing.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() > deadline) {
            const AudioGuard guard(impl.audioGuard);
            impl.flushParams();
            return;
        }
        std::this_thread::yield();
    }
}

double ClapPlugin::processValue(std::uint32_t /*id*/, double value) const { return value; }

bool ClapPlugin::takeParamsChanged() { return impl_->paramsChanged.exchange(false, std::memory_order_relaxed); }

}  // namespace emerick::engine
