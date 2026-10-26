// Everything here runs on the audio thread: it queues MIDI for a CLAP instrument and runs its process call on
// buffers and event lists the main thread allocated when it activated the plugin.

#include <algorithm>

#include "clap_host.hpp"
#include "clock.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

clap_event_header header(std::uint32_t size, std::uint32_t time, std::uint16_t type) {
    return {.size = size, .time = time, .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = type, .flags = 0};
}

// Notes go as CLAP note events to a plugin that reads them and every other channel message as MIDI.
void addEvent(ClapInstrument::Impl& impl, const QueuedMidi& message, std::uint32_t time) {
    const std::uint8_t type = message.bytes[0] & 0xF0;
    const std::size_t index = impl.eventCount;
    const bool note = type == core::midi::kNoteOn || type == core::midi::kNoteOff;
    if (impl.clapNotes && note && message.size == 3) {
        const bool on = type == core::midi::kNoteOn && message.bytes[2] > 0;
        impl.notes[index] = {
            .header = header(sizeof(clap_event_note), time, on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF),
            .note_id = -1,
            .port_index = 0,
            .channel = static_cast<std::int16_t>(message.bytes[0] & 0x0F),
            .key = message.bytes[1],
            .velocity = message.bytes[2] / 127.0};
        impl.events[index] = &impl.notes[index].header;
    } else {
        impl.midi[index] = {.header = header(sizeof(clap_event_midi), time, CLAP_EVENT_MIDI),
                            .port_index = 0,
                            .data = {message.bytes[0], message.bytes[1], message.bytes[2]}};
        impl.events[index] = &impl.midi[index].header;
    }
    ++impl.eventCount;
}

// CLAP wants events in time order, and an insertion sort keeps messages at the same time in the order they came.
void sortEvents(ClapInstrument::Impl& impl) {
    const auto events = std::span(impl.events).first(impl.eventCount);
    for (std::size_t index = 1; index < events.size(); ++index) {
        const clap_event_header* moving = events[index];
        std::size_t at = index;
        for (; at > 0 && events[at - 1]->time > moving->time; --at) {
            events[at] = events[at - 1];
        }
        events[at] = moving;
    }
}

void collectEvents(ClapInstrument::Impl& impl, core::Frames frames, std::int64_t hostTimeNs) {
    impl.eventCount = 0;
    for (const QueuedMidi& message : std::span(impl.queued).first(impl.queuedCount)) {
        const core::Frames offset =
            std::clamp<core::Frames>(nsToFrames(message.timestampNs - hostTimeNs, impl.sampleRate), 0, frames - 1);
        addEvent(impl, message, static_cast<std::uint32_t>(offset));
    }
    impl.queuedCount = 0;
    sortEvents(impl);
}

void mixInto(const ClapInstrument::Impl& impl, std::span<float* const> mix, core::Frames frames) {
    if (impl.outputPorts.empty() || impl.outputPorts.front().channels.empty()) {
        return;
    }
    const auto& outputs = impl.outputPorts.front().channels;
    for (std::size_t channel = 0; channel < mix.size(); ++channel) {
        const float* source = outputs[std::min(channel, outputs.size() - 1)].data();
        std::transform(source, source + frames, mix[channel], mix[channel], std::plus{});
    }
}

}  // namespace

void ClapInstrument::queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    Impl& impl = *impl_;
    if (bytes.empty() || bytes.size() > 3 || !core::midi::isChannelMessage(bytes[0]) ||
        impl.queuedCount == impl.queued.size()) {
        return;
    }
    QueuedMidi& message = impl.queued[impl.queuedCount++];
    message.timestampNs = timestampNs;
    message.size = static_cast<std::uint8_t>(bytes.size());
    message.bytes = {};
    std::ranges::copy(bytes, message.bytes.begin());
}

void ClapInstrument::processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs) {
    Impl& impl = *impl_;
    if (frames <= 0 || impl.audioGuard.exchange(true, std::memory_order_acquire)) {
        return;
    }
    const AudioThreadScope audioThread;
    if (impl.active && frames <= impl.maxFrames && !impl.processing) {
        impl.processing = impl.proxy->startProcessing();
    }
    if (impl.active && frames <= impl.maxFrames && impl.processing) {
        collectEvents(impl, frames, hostTimeNs);
        for (PortBuffers& port : impl.outputPorts) {
            for (std::vector<float>& channel : port.channels) {
                std::fill_n(channel.data(), frames, 0.0F);
            }
        }
        const clap_input_events input{
            .ctx = &impl,
            .size =
                [](const clap_input_events* list) {
                    return static_cast<std::uint32_t>(static_cast<const Impl*>(list->ctx)->eventCount);
                },
            .get = [](const clap_input_events* list,
                      std::uint32_t index) { return static_cast<const Impl*>(list->ctx)->events[index]; }};
        const clap_output_events output{
            .ctx = nullptr, .try_push = [](const clap_output_events*, const clap_event_header*) { return true; }};
        const clap_process process{.steady_time = -1,
                                   .frames_count = static_cast<std::uint32_t>(frames),
                                   .transport = nullptr,
                                   .audio_inputs = impl.inputBuffers.data(),
                                   .audio_outputs = impl.outputBuffers.data(),
                                   .audio_inputs_count = static_cast<std::uint32_t>(impl.inputBuffers.size()),
                                   .audio_outputs_count = static_cast<std::uint32_t>(impl.outputBuffers.size()),
                                   .in_events = &input,
                                   .out_events = &output};
        if (impl.proxy->process(&process) != CLAP_PROCESS_ERROR) {
            mixInto(impl, mix, frames);
        }
    }
    impl.queuedCount = 0;
    impl.audioGuard.store(false, std::memory_order_release);
}

}  // namespace emerick::engine
