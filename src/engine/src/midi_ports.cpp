#include "midi_ports.hpp"

#include "clock.hpp"

namespace emerick::engine {

namespace {

constexpr std::size_t kMidiRingBytes = std::size_t{1} << 18;

}  // namespace

std::string missingMidiPort(std::string_view portId, bool input) {
    return std::string{"the MIDI "} + (input ? "input " : "output ") + std::string{portId} +
           " is missing, so the track plays as if routed to none until it returns";
}

MidiInputPort::MidiInputPort(MidiBackend& backend, const std::string& portId)
    : ring(kMidiRingBytes),
      input(backend.openInput(portId, [this](std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
          if (!writeRecord(ring, MidiRecordHeader{timestampNs, static_cast<std::uint32_t>(bytes.size())}, bytes)) {
              lost.fetch_add(1, std::memory_order_relaxed);
          }
      })) {}

MidiOutputPort::MidiOutputPort(MidiBackend& backend, const std::string& portId)
    : id_(portId), ring_(kMidiRingBytes), output_(backend.openOutput(portId)) {}

bool MidiOutputPort::queue(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    unsent_.fetch_add(1, std::memory_order_relaxed);
    if (writeRecord(ring_, MidiRecordHeader{timestampNs, static_cast<std::uint32_t>(bytes.size())}, bytes)) {
        return true;
    }
    unsent_.fetch_sub(1, std::memory_order_release);
    return false;
}

// We hold messages in timestamp order, so each one goes out when it is due in whatever order the audio thread queued
// it.
void MidiOutputPort::poll() {
    MidiRecordHeader header;
    while (readValue(ring_, header)) {
        std::vector<std::uint8_t> bytes(header.size);
        readInto(ring_, std::span(bytes));
        waiting_.emplace(header.timestampNs, std::move(bytes));
    }
    const std::int64_t now = steadyNowNs();
    while (!waiting_.empty() && waiting_.begin()->first <= now) {
        const auto node = waiting_.extract(waiting_.begin());
        output_->send(node.mapped(), node.key());
        unsent_.fetch_sub(1, std::memory_order_release);
    }
}

}  // namespace emerick::engine
