#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "emerick/engine/midi_backend.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

struct MidiRecordHeader {
    std::int64_t timestampNs = 0;
    std::uint32_t size = 0;
};

struct MidiInputPort {
    MidiInputPort(MidiBackend& backend, const std::string& portId);

    SpscRing ring;
    // Messages the full ring turned away; the audio thread reads and resets it.
    std::atomic<std::uint32_t> lost{0};
    // We declare the input after the ring so the input stops writing before the ring goes away.
    std::unique_ptr<MidiInput> input;
};

// We call queue on the audio thread and poll on the sender thread, which sends every message in timestamp order once
// it is due.
class MidiOutputPort {
public:
    MidiOutputPort(MidiBackend& backend, const std::string& portId);

    bool queue(std::span<const std::uint8_t> bytes, std::int64_t timestampNs);
    void poll();
    [[nodiscard]] bool flushed() const { return unsent_.load(std::memory_order_acquire) == 0; }

private:
    SpscRing ring_;
    std::unique_ptr<MidiOutput> output_;
    std::multimap<std::int64_t, std::vector<std::uint8_t>> waiting_;
    std::atomic<std::size_t> unsent_{0};
};

}  // namespace emerick::engine
