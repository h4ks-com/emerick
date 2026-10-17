// libremidi's input thread calls the receive callback for every incoming message.

#include <algorithm>
#include <libremidi/libremidi.hpp>
#include <stdexcept>

#include "emerick/engine/midi_backend.hpp"

namespace emerick::engine {

namespace {

template <class Port>
Port findPort(const std::vector<Port>& ports, const std::string& portId) {
    const auto found = std::ranges::find(ports, portId, &Port::display_name);
    if (found == ports.end()) {
        throw std::invalid_argument("unknown MIDI port " + portId);
    }
    return *found;
}

class LibremidiInput final : public MidiInput {
public:
    LibremidiInput(const libremidi::input_port& port, MidiReceive receive)
        : receive_(std::move(receive)),
          input_(libremidi::input_configuration{
              .on_message = [this](const libremidi::message& message) { receive_(message, message.timestamp); },
              .ignore_sysex = 0U,
              .timestamps = libremidi::timestamp_mode::SystemMonotonic}) {
        if (const auto error = input_.open_port(port, "emerick"); error != stdx::error{}) {
            throw std::runtime_error("cannot open MIDI input " + port.display_name);
        }
    }

private:
    MidiReceive receive_;
    libremidi::midi_in input_;
};

class LibremidiOutput final : public MidiOutput {
public:
    explicit LibremidiOutput(const libremidi::output_port& port) {
        if (const auto error = output_.open_port(port, "emerick"); error != stdx::error{}) {
            throw std::runtime_error("cannot open MIDI output " + port.display_name);
        }
    }

    // The engine's sender thread calls this when the timestamp is due, so we send right away.
    void send(std::span<const std::uint8_t> bytes, std::int64_t /*timestampNs*/) override {
        static_cast<void>(output_.send_message(bytes));
    }

private:
    libremidi::midi_out output_;
};

}  // namespace

struct LibremidiBackend::Impl {
    libremidi::observer observer;
};

LibremidiBackend::LibremidiBackend() : impl_(std::make_unique<Impl>()) {}

LibremidiBackend::~LibremidiBackend() = default;

std::vector<MidiPortInfo> LibremidiBackend::ports() {
    std::vector<MidiPortInfo> result;
    for (const libremidi::input_port& port : impl_->observer.get_input_ports()) {
        result.push_back({port.display_name, port.display_name, true});
    }
    for (const libremidi::output_port& port : impl_->observer.get_output_ports()) {
        result.push_back({port.display_name, port.display_name, false});
    }
    return result;
}

std::unique_ptr<MidiInput> LibremidiBackend::openInput(const std::string& portId, MidiReceive receive) {
    return std::make_unique<LibremidiInput>(findPort(impl_->observer.get_input_ports(), portId), std::move(receive));
}

std::unique_ptr<MidiOutput> LibremidiBackend::openOutput(const std::string& portId) {
    return std::make_unique<LibremidiOutput>(findPort(impl_->observer.get_output_ports(), portId));
}

}  // namespace emerick::engine
