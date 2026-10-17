#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::engine {

struct MidiPortInfo {
    std::string id;
    std::string name;
    bool isInput = false;
};

// Timestamps are steady clock nanoseconds, the clock AudioBlock::hostTimeNs uses.
using MidiReceive = std::function<void(std::span<const std::uint8_t> bytes, std::int64_t timestampNs)>;

class MidiInput {
public:
    MidiInput() = default;
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;
    MidiInput(MidiInput&&) = delete;
    MidiInput& operator=(MidiInput&&) = delete;
    virtual ~MidiInput() = default;
};

class MidiOutput {
public:
    MidiOutput() = default;
    MidiOutput(const MidiOutput&) = delete;
    MidiOutput& operator=(const MidiOutput&) = delete;
    MidiOutput(MidiOutput&&) = delete;
    MidiOutput& operator=(MidiOutput&&) = delete;
    virtual ~MidiOutput() = default;

    virtual void send(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) = 0;
};

class MidiBackend {
public:
    MidiBackend() = default;
    MidiBackend(const MidiBackend&) = delete;
    MidiBackend& operator=(const MidiBackend&) = delete;
    MidiBackend(MidiBackend&&) = delete;
    MidiBackend& operator=(MidiBackend&&) = delete;
    virtual ~MidiBackend() = default;

    [[nodiscard]] virtual std::vector<MidiPortInfo> ports() = 0;
    // The receive callback runs on a MIDI thread until the returned input is destroyed.
    virtual std::unique_ptr<MidiInput> openInput(const std::string& portId, MidiReceive receive) = 0;
    virtual std::unique_ptr<MidiOutput> openOutput(const std::string& portId) = 0;
};

struct SentMidi {
    std::vector<std::uint8_t> bytes;
    std::int64_t timestampNs = 0;
};

// In-process ports: tests and agents inject input and read back what the engine sent.
class VirtualMidiBackend final : public MidiBackend {
public:
    VirtualMidiBackend(std::vector<std::string> inputs, std::vector<std::string> outputs);
    ~VirtualMidiBackend() override = default;
    VirtualMidiBackend(const VirtualMidiBackend&) = delete;
    VirtualMidiBackend& operator=(const VirtualMidiBackend&) = delete;
    VirtualMidiBackend(VirtualMidiBackend&&) = delete;
    VirtualMidiBackend& operator=(VirtualMidiBackend&&) = delete;

    [[nodiscard]] std::vector<MidiPortInfo> ports() override;
    std::unique_ptr<MidiInput> openInput(const std::string& portId, MidiReceive receive) override;
    std::unique_ptr<MidiOutput> openOutput(const std::string& portId) override;

    void inject(std::string_view portId, std::span<const std::uint8_t> bytes, std::int64_t timestampNs);
    [[nodiscard]] std::vector<SentMidi> sent(std::string_view portId) const;
    void clearSent();

private:
    class Input;
    class Output;

    std::vector<std::string> inputs_;
    std::vector<std::string> outputs_;
    mutable std::mutex mutex_;
    std::map<std::string, MidiReceive, std::less<>> receivers_;
    std::map<std::string, std::vector<SentMidi>, std::less<>> sent_;
};

class LibremidiBackend final : public MidiBackend {
public:
    LibremidiBackend();
    ~LibremidiBackend() override;
    LibremidiBackend(const LibremidiBackend&) = delete;
    LibremidiBackend& operator=(const LibremidiBackend&) = delete;
    LibremidiBackend(LibremidiBackend&&) = delete;
    LibremidiBackend& operator=(LibremidiBackend&&) = delete;

    [[nodiscard]] std::vector<MidiPortInfo> ports() override;
    std::unique_ptr<MidiInput> openInput(const std::string& portId, MidiReceive receive) override;
    std::unique_ptr<MidiOutput> openOutput(const std::string& portId) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emerick::engine
