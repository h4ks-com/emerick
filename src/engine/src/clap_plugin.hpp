#pragma once

#include <memory>

#include "emerick/engine/plugin.hpp"

namespace emerick::engine {

class ClapPlugin final : public Plugin {
public:
    struct Impl;

    explicit ClapPlugin(std::unique_ptr<Impl> impl);
    ~ClapPlugin() override;
    ClapPlugin(const ClapPlugin&) = delete;
    ClapPlugin& operator=(const ClapPlugin&) = delete;
    ClapPlugin(ClapPlugin&&) = delete;
    ClapPlugin& operator=(ClapPlugin&&) = delete;

    [[nodiscard]] const std::string& name() const override;
    [[nodiscard]] bool producesMidi() const override;
    bool activate(double sampleRate, core::Frames maxFrames) override;
    [[nodiscard]] core::Frames latency() const override;
    bool idle() override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> saveState() const override;
    bool loadState(std::span<const std::uint8_t> bytes) override;

    [[nodiscard]] std::vector<PluginParam> params() const override;
    [[nodiscard]] std::optional<double> paramValue(std::uint32_t id) const override;
    [[nodiscard]] std::string paramText(std::uint32_t id, double value) const override;
    void setParam(std::uint32_t id, double value) override;
    bool takeParamsChanged() override;
    [[nodiscard]] double processValue(std::uint32_t id, double value) const override;

    [[nodiscard]] bool hasEditor() const override;
    [[nodiscard]] bool editorOpen() const override;
    bool openFloatingEditor(const std::string& title) override;
    std::optional<EditorSize> embedEditor(void* parent) override;
    void closeEditor() override;
    std::optional<EditorSize> takeEditorResize() override;

    void queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) override;
    void queueParam(std::uint32_t id, double value) override;
    void processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs,
                      const InstrumentTransport& transport) override;
    void processEffect(std::span<float* const> channels, core::Frames frames, std::int64_t hostTimeNs,
                       const InstrumentTransport& transport) override;
    [[nodiscard]] std::span<const InstrumentMidi> producedMidi() const override;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
