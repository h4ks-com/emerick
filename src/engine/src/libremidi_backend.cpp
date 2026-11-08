// libremidi's input thread calls the receive callback for every incoming message, and its observer reports added and
// removed ports on a system MIDI thread.

#include <algorithm>
#include <libremidi/libremidi.hpp>
#include <optional>
#include <stdexcept>

#include "emerick/engine/midi_backend.hpp"

namespace emerick::engine {

namespace {

template <class Port>
std::optional<Port> findPort(const std::vector<Port>& ports, const std::string& portId) {
    const auto found = std::ranges::find(ports, portId, &Port::display_name);
    return found == ports.end() ? std::nullopt : std::optional{*found};
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
    // We watch removals of every kind of port, because a removed CoreMIDI endpoint no longer reads as hardware.
    libremidi::observer removals;
};

LibremidiBackend::LibremidiBackend()
    : impl_(std::make_unique<Impl>(libremidi::observer{libremidi::observer_configuration{
                                       .input_added = [this](const libremidi::input_port&) { portListChanged(); },
                                       .input_removed = [this](const libremidi::input_port&) { portListChanged(); },
                                       .output_added = [this](const libremidi::output_port&) { portListChanged(); },
                                       .output_removed = [this](const libremidi::output_port&) { portListChanged(); },
                                       .notify_in_constructor = 0U}},
                                   libremidi::observer{libremidi::observer_configuration{
                                       .input_removed = [this](const libremidi::input_port&) { portListChanged(); },
                                       .output_removed = [this](const libremidi::output_port&) { portListChanged(); },
                                       .track_any = 1U,
                                       .notify_in_constructor = 0U}})) {}

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
    const auto port = findPort(impl_->observer.get_input_ports(), portId);
    if (!port) {
        throw std::invalid_argument("unknown MIDI port " + portId);
    }
    return std::make_unique<LibremidiInput>(*port, std::move(receive));
}

std::unique_ptr<MidiOutput> LibremidiBackend::openOutput(const std::string& portId) {
    const auto port = findPort(impl_->observer.get_output_ports(), portId);
    if (!port) {
        throw std::invalid_argument("unknown MIDI port " + portId);
    }
    return std::make_unique<LibremidiOutput>(*port);
}

}  // namespace emerick::engine
