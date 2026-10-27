// Everything here runs on the audio thread: it queues MIDI for a CLAP instrument and runs its process call on
// buffers and event lists the main thread allocated when it activated the plugin.

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "clap_host.hpp"
#include "clock.hpp"
#include "emerick/core/midi.hpp"

namespace emerick::engine {

namespace {

clap_event_header header(std::uint32_t size, std::uint32_t time, std::uint16_t type) {
    return {.size = size, .time = time, .space_id = CLAP_CORE_EVENT_SPACE_ID, .type = type, .flags = 0};
}

// Notes go as CLAP note events to a plugin that reads them, every other channel message as MIDI, and a sysex as a
// sysex whose bytes stay in the queue's pool through the process call.
void addEvent(ClapInstrument::Impl& impl, const QueuedMidi& message, std::uint32_t time) {
    const auto bytes = std::span(impl.queued.bytes).subspan(message.offset, message.size);
    const std::size_t index = impl.eventCount;
    if (bytes[0] == core::midi::kSysexStart) {
        if (!impl.takesSysex) {
            return;
        }
        impl.sysex[index] = {.header = header(sizeof(clap_event_midi_sysex), time, CLAP_EVENT_MIDI_SYSEX),
                             .port_index = 0,
                             .buffer = bytes.data(),
                             .size = static_cast<std::uint32_t>(bytes.size())};
        impl.events[index] = &impl.sysex[index].header;
        ++impl.eventCount;
        return;
    }
    const std::uint8_t type = bytes[0] & 0xF0;
    const bool note = type == core::midi::kNoteOn || type == core::midi::kNoteOff;
    if (impl.clapNotes && note && bytes.size() == 3) {
        const bool on = type == core::midi::kNoteOn && bytes[2] > 0;
        impl.notes[index] = {
            .header = header(sizeof(clap_event_note), time, on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF),
            .note_id = -1,
            .port_index = 0,
            .channel = static_cast<std::int16_t>(bytes[0] & 0x0F),
            .key = bytes[1],
            .velocity = bytes[2] / 127.0};
        impl.events[index] = &impl.notes[index].header;
    } else {
        std::array<std::uint8_t, 3> data{};
        std::ranges::copy(bytes, data.begin());
        impl.midi[index] = {.header = header(sizeof(clap_event_midi), time, CLAP_EVENT_MIDI),
                            .port_index = 0,
                            .data = {data[0], data[1], data[2]}};
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
    for (const QueuedMidi& message : std::span(impl.queued.messages).first(impl.queued.count)) {
        const core::Frames offset =
            std::clamp<core::Frames>(nsToFrames(message.timestampNs - hostTimeNs, impl.sampleRate), 0, frames - 1);
        addEvent(impl, message, static_cast<std::uint32_t>(offset));
    }
    impl.collectParams();
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

void ClapInstrument::Impl::collectParams() {
    paramEventCount = 0;
    QueuedParam change;
    while (paramEventCount < paramEvents.size() && readValue(paramQueue, change)) {
        clap_event_param_value& event = paramEvents[paramEventCount++];
        event = {.header = header(sizeof(clap_event_param_value), 0, CLAP_EVENT_PARAM_VALUE),
                 .param_id = change.id,
                 .cookie = nullptr,
                 .note_id = -1,
                 .port_index = -1,
                 .channel = -1,
                 .key = -1,
                 .value = change.value};
        events[eventCount++] = &event.header;
    }
}

clap_input_events ClapInstrument::Impl::inputEvents() {
    return {.ctx = this,
            .size =
                [](const clap_input_events* list) {
                    return static_cast<std::uint32_t>(static_cast<const Impl*>(list->ctx)->eventCount);
                },
            .get = [](const clap_input_events* list,
                      std::uint32_t index) { return static_cast<const Impl*>(list->ctx)->events[index]; }};
}

clap_output_events ClapInstrument::Impl::outputEvents() {
    return {.ctx = this, .try_push = [](const clap_output_events* list, const clap_event_header* event) {
                if (event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
                    static_cast<Impl*>(list->ctx)->paramsChanged.store(true, std::memory_order_relaxed);
                }
                return true;
            }};
}

namespace {

// CLAP hands every event as its header, and the type in that header says which struct it starts.
template <typename Event>
const Event& eventAs(const clap_event_header& header) {
    return *reinterpret_cast<const Event*>(&header);
}

std::uint8_t velocityByte(double velocity, bool on) {
    const auto scaled = static_cast<std::uint8_t>(std::clamp(std::lround(velocity * 127.0), 0L, 127L));
    return on ? std::max<std::uint8_t>(scaled, 1) : scaled;
}

// The plugin's notes come out as MIDI note messages, so a track that takes them records and plays them as MIDI.
void produce(ClapInstrument::Impl& impl, const clap_event_header& event) {
    std::array<std::uint8_t, 3> message{};
    std::span<const std::uint8_t> bytes;
    switch (event.type) {
        case CLAP_EVENT_NOTE_ON:
        case CLAP_EVENT_NOTE_OFF: {
            const auto& note = eventAs<clap_event_note>(event);
            if (note.key < 0 || note.key > 127) {
                return;
            }
            const bool on = event.type == CLAP_EVENT_NOTE_ON;
            message = {static_cast<std::uint8_t>((on ? core::midi::kNoteOn : core::midi::kNoteOff) |
                                                 (std::max<std::int16_t>(note.channel, 0) & 0x0F)),
                       static_cast<std::uint8_t>(note.key), velocityByte(note.velocity, on)};
            bytes = message;
            break;
        }
        case CLAP_EVENT_MIDI: {
            const auto& midi = eventAs<clap_event_midi>(event);
            std::ranges::copy(midi.data, message.begin());
            bytes = std::span(message).first(core::midi::channelMessageSize(message[0]));
            break;
        }
        case CLAP_EVENT_MIDI_SYSEX: {
            const auto& sysex = eventAs<clap_event_midi_sysex>(event);
            if (sysex.buffer == nullptr || sysex.size == 0) {
                return;
            }
            bytes = std::span(sysex.buffer, sysex.size);
            break;
        }
        default:
            return;
    }
    if (bytes.empty()) {
        return;
    }
    if (const auto stored = impl.produced.store(bytes)) {
        impl.produced.messages[impl.produced.count++] = {.offset = event.time, .bytes = *stored};
    }
}

}  // namespace

clap_output_events ClapInstrument::Impl::processOutputEvents() {
    return {.ctx = this, .try_push = [](const clap_output_events* list, const clap_event_header* event) {
                auto& impl = *static_cast<Impl*>(list->ctx);
                if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
                    return true;
                }
                if (event->type == CLAP_EVENT_PARAM_VALUE) {
                    impl.paramsChanged.store(true, std::memory_order_relaxed);
                } else {
                    produce(impl, *event);
                }
                return true;
            }};
}

void ClapInstrument::queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    Impl& impl = *impl_;
    const bool sysex = !bytes.empty() && bytes[0] == core::midi::kSysexStart;
    if (bytes.empty() || (!sysex && (bytes.size() > 3 || !core::midi::isChannelMessage(bytes[0])))) {
        return;
    }
    const std::size_t offset = impl.queued.used;
    if (const auto stored = impl.queued.store(bytes)) {
        impl.queued.messages[impl.queued.count++] = {
            .timestampNs = timestampNs, .offset = offset, .size = bytes.size()};
    }
}

std::span<const InstrumentMidi> ClapInstrument::producedMidi() const {
    return std::span(impl_->produced.messages).first(impl_->produced.count);
}

void ClapInstrument::processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs) {
    Impl& impl = *impl_;
    impl.produced.clear();
    if (frames <= 0 || impl.audioGuard.exchange(true, std::memory_order_acquire)) {
        return;
    }
    const AudioThreadScope audioThread;
    if (impl.active && frames <= impl.maxFrames && !impl.processing.load(std::memory_order_relaxed)) {
        impl.processing.store(impl.proxy->startProcessing(), std::memory_order_relaxed);
    }
    if (impl.active && frames <= impl.maxFrames && impl.processing.load(std::memory_order_relaxed)) {
        collectEvents(impl, frames, hostTimeNs);
        for (PortBuffers& port : impl.outputPorts) {
            for (std::vector<float>& channel : port.channels) {
                std::fill_n(channel.data(), frames, 0.0F);
            }
        }
        const clap_input_events input = impl.inputEvents();
        const clap_output_events output = impl.processOutputEvents();
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
        impl.paramsApplied.fetch_add(impl.paramEventCount, std::memory_order_release);
    }
    impl.queued.clear();
    impl.audioGuard.store(false, std::memory_order_release);
}

}  // namespace emerick::engine
