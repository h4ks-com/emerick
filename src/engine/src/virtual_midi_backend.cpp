#include <algorithm>
#include <stdexcept>

#include "emerick/engine/midi_backend.hpp"

namespace emerick::engine {

class VirtualMidiBackend::Input final : public MidiInput {
public:
    Input(VirtualMidiBackend& backend, std::string portId) : backend_(backend), portId_(std::move(portId)) {}
    ~Input() override {
        const std::scoped_lock lock(backend_.mutex_);
        backend_.receivers_.erase(portId_);
    }
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;

private:
    VirtualMidiBackend& backend_;
    std::string portId_;
};

class VirtualMidiBackend::Output final : public MidiOutput {
public:
    Output(VirtualMidiBackend& backend, std::string portId) : backend_(backend), portId_(std::move(portId)) {}

    void send(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) override {
        const std::scoped_lock lock(backend_.mutex_);
        backend_.sent_[portId_].push_back({{bytes.begin(), bytes.end()}, timestampNs});
    }

private:
    VirtualMidiBackend& backend_;
    std::string portId_;
};

VirtualMidiBackend::VirtualMidiBackend(std::vector<std::string> inputs, std::vector<std::string> outputs)
    : inputs_(std::move(inputs)), outputs_(std::move(outputs)) {}

std::vector<MidiPortInfo> VirtualMidiBackend::ports() {
    std::vector<MidiPortInfo> result;
    result.reserve(inputs_.size() + outputs_.size());
    for (const std::string& name : inputs_) {
        result.push_back({name, name, true});
    }
    for (const std::string& name : outputs_) {
        result.push_back({name, name, false});
    }
    return result;
}

std::unique_ptr<MidiInput> VirtualMidiBackend::openInput(const std::string& portId, MidiReceive receive) {
    if (std::ranges::find(inputs_, portId) == inputs_.end()) {
        throw std::invalid_argument("unknown MIDI input " + portId);
    }
    const std::scoped_lock lock(mutex_);
    receivers_[portId] = std::move(receive);
    return std::make_unique<Input>(*this, portId);
}

std::unique_ptr<MidiOutput> VirtualMidiBackend::openOutput(const std::string& portId) {
    if (std::ranges::find(outputs_, portId) == outputs_.end()) {
        throw std::invalid_argument("unknown MIDI output " + portId);
    }
    return std::make_unique<Output>(*this, portId);
}

void VirtualMidiBackend::inject(std::string_view portId, std::span<const std::uint8_t> bytes,
                                std::int64_t timestampNs) {
    const std::scoped_lock lock(mutex_);
    if (const auto receiver = receivers_.find(portId); receiver != receivers_.end()) {
        receiver->second(bytes, timestampNs);
    }
}

std::vector<SentMidi> VirtualMidiBackend::sent(std::string_view portId) const {
    const std::scoped_lock lock(mutex_);
    const auto found = sent_.find(portId);
    return found == sent_.end() ? std::vector<SentMidi>{} : found->second;
}

void VirtualMidiBackend::clearSent() {
    const std::scoped_lock lock(mutex_);
    sent_.clear();
}

}  // namespace emerick::engine
