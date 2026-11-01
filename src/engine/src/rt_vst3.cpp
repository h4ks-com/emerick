// Everything here runs on the audio thread: it queues notes and params for a VST3 plugin and runs its process call on
// buffers, param changes and event lists the main thread allocated when it activated the plugin.

#include <pluginterfaces/vst/ivstprocesscontext.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <utility>

#include "clock.hpp"
#include "emerick/core/midi.hpp"
#include "vst3_host.hpp"

namespace emerick::engine {

Steinberg::tresult PLUGIN_API ParamQueue::getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                                   vst::ParamValue& value) {
    if (index < 0 || std::cmp_greater_equal(index, count_)) {
        return Steinberg::kResultFalse;
    }
    const Point& point = points_.at(static_cast<std::size_t>(index));
    sampleOffset = point.offset;
    value = point.value;
    return Steinberg::kResultOk;
}

// Points stay in time order, and a point at an offset that has one replaces it.
Steinberg::tresult PLUGIN_API ParamQueue::addPoint(Steinberg::int32 sampleOffset, vst::ParamValue value,
                                                   Steinberg::int32& index) {
    const auto used = std::span(points_).first(count_);
    const auto at = std::ranges::lower_bound(used, sampleOffset, {}, &Point::offset);
    const auto position = static_cast<std::size_t>(at - used.begin());
    if (at != used.end() && at->offset == sampleOffset) {
        at->value = value;
    } else if (count_ == points_.size()) {
        return Steinberg::kResultFalse;
    } else {
        std::shift_right(points_.begin() + static_cast<std::ptrdiff_t>(position),
                         points_.begin() + static_cast<std::ptrdiff_t>(count_ + 1), 1);
        points_.at(position) = {.offset = sampleOffset, .value = value};
        ++count_;
    }
    index = static_cast<Steinberg::int32>(position);
    return Steinberg::kResultOk;
}

vst::IParamValueQueue* PLUGIN_API ParamChanges::getParameterData(Steinberg::int32 index) {
    return index >= 0 && std::cmp_less(index, used_) ? &queues_.at(static_cast<std::size_t>(index)) : nullptr;
}

vst::IParamValueQueue* PLUGIN_API ParamChanges::addParameterData(const vst::ParamID& id, Steinberg::int32& index) {
    const auto used = std::span(queues_).first(used_);
    const auto found = std::ranges::find_if(used, [id](ParamQueue& queue) { return queue.getParameterId() == id; });
    if (found != used.end()) {
        index = static_cast<Steinberg::int32>(found - used.begin());
        return &*found;
    }
    if (used_ == queues_.size()) {
        return nullptr;
    }
    ParamQueue& added = queues_.at(used_);
    added.reset(id);
    index = static_cast<Steinberg::int32>(used_++);
    return &added;
}

Steinberg::tresult PLUGIN_API EventList::getEvent(Steinberg::int32 index, vst::Event& event) {
    if (index < 0 || std::cmp_greater_equal(index, count_)) {
        return Steinberg::kResultFalse;
    }
    event = events_.at(static_cast<std::size_t>(index));
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API EventList::addEvent(vst::Event& event) {
    if (count_ == events_.size()) {
        return Steinberg::kResultFalse;
    }
    events_.at(count_++) = event;
    return Steinberg::kResultOk;
}

namespace {

void addChange(ParamChanges& changes, const Vst3ParamChange& change) {
    Steinberg::int32 index = 0;
    if (vst::IParamValueQueue* queue = changes.addParameterData(change.id, index)) {
        queue->addPoint(0, change.value, index);
    }
}

// We hand the plugin its notes; other channel messages wait for MIDI controller mapping.
void addNotes(Vst3Plugin::Impl& impl, core::Frames frames, std::int64_t hostTimeNs) {
    impl.events.clear();
    for (const QueuedNote& note : std::span(impl.notes).first(impl.noteCount)) {
        const std::uint8_t type = note.bytes[0] & 0xF0;
        const auto channel = static_cast<Steinberg::int16>(note.bytes[0] & 0x0F);
        const auto offset =
            std::clamp<core::Frames>(nsToFrames(note.timestampNs - hostTimeNs, impl.sampleRate), 0, frames - 1);
        vst::Event event{};
        event.busIndex = 0;
        event.sampleOffset = static_cast<Steinberg::int32>(offset);
        event.flags = vst::Event::kIsLive;
        const float velocity = static_cast<float>(note.bytes[2]) / 127.0F;
        // vst::Event keeps its payload in a union, and its type says which member is set.
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        if (type == core::midi::kNoteOn && note.bytes[2] > 0) {
            event.type = vst::Event::kNoteOnEvent;
            event.noteOn = {.channel = channel,
                            .pitch = note.bytes[1],
                            .tuning = 0.0F,
                            .velocity = velocity,
                            .length = 0,
                            .noteId = -1};
        } else {
            event.type = vst::Event::kNoteOffEvent;
            event.noteOff = {
                .channel = channel, .pitch = note.bytes[1], .velocity = velocity, .noteId = -1, .tuning = 0.0F};
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        impl.events.addEvent(event);
    }
    impl.noteCount = 0;
}

vst::ProcessContext processContext(const InstrumentTransport& transport, double sampleRate) {
    const double quartersPerBar = 4.0 * transport.meter.numerator / transport.meter.denominator;
    Steinberg::uint32 state = vst::ProcessContext::kTempoValid | vst::ProcessContext::kTimeSigValid |
                              vst::ProcessContext::kProjectTimeMusicValid | vst::ProcessContext::kBarPositionValid;
    if (transport.playing) {
        state |= vst::ProcessContext::kPlaying;
    }
    if (transport.looping) {
        state |= vst::ProcessContext::kCycleActive | vst::ProcessContext::kCycleValid;
    }
    vst::ProcessContext context{};
    context.state = state;
    context.sampleRate = sampleRate;
    context.projectTimeSamples = std::llround(transport.beats * 60.0 / transport.bpm * sampleRate);
    context.projectTimeMusic = transport.beats;
    context.barPositionMusic = std::floor(transport.beats / quartersPerBar) * quartersPerBar;
    context.cycleStartMusic = transport.loopStartBeats;
    context.cycleEndMusic = transport.loopEndBeats;
    context.tempo = transport.bpm;
    context.timeSigNumerator = transport.meter.numerator;
    context.timeSigDenominator = transport.meter.denominator;
    return context;
}

}  // namespace

bool Vst3Plugin::Impl::process(core::Frames frames, std::int64_t hostTimeNs, const InstrumentTransport& transport) {
    if (!active || frames > maxFrames) {
        return false;
    }
    inputChanges.clear();
    outputChanges.clear();
    std::uint64_t sent = 0;
    Vst3ParamChange change;
    while (readValue(paramQueue, change)) {
        addChange(inputChanges, change);
        ++sent;
    }
    for (const Vst3ParamChange& automatedChange : std::span(automated).first(automatedCount)) {
        addChange(inputChanges, automatedChange);
    }
    automatedCount = 0;
    if (frames > 0) {
        addNotes(*this, frames, hostTimeNs);
    }
    for (BusBuffers& bus : outputs) {
        for (std::vector<float>& channel : bus.channels) {
            std::fill_n(channel.data(), frames, 0.0F);
        }
    }
    vst::ProcessContext context = processContext(transport, sampleRate);
    vst::ProcessData data{};
    data.processMode = vst::kRealtime;
    data.symbolicSampleSize = vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(frames);
    data.numInputs = static_cast<Steinberg::int32>(inputBuses.size());
    data.numOutputs = static_cast<Steinberg::int32>(outputBuses.size());
    data.inputs = inputBuses.data();
    data.outputs = outputBuses.data();
    data.inputParameterChanges = &inputChanges;
    data.outputParameterChanges = &outputChanges;
    data.inputEvents = takesNotes ? &events : nullptr;
    data.processContext = &context;
    const bool played = processor->process(data) == Steinberg::kResultOk;
    if (!outputChanges.empty()) {
        paramsChanged.store(true, std::memory_order_relaxed);
    }
    events.clear();
    paramsApplied.fetch_add(sent, std::memory_order_release);
    return played;
}

void Vst3Plugin::queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    Impl& impl = *impl_;
    if (bytes.size() != 3 || impl.noteCount == impl.notes.size()) {
        return;
    }
    const std::uint8_t type = bytes[0] & 0xF0;
    if (type != core::midi::kNoteOn && type != core::midi::kNoteOff) {
        return;
    }
    QueuedNote& note = impl.notes.at(impl.noteCount++);
    note.timestampNs = timestampNs;
    std::ranges::copy(bytes, note.bytes.begin());
}

void Vst3Plugin::queueParam(std::uint32_t id, double value) {
    Impl& impl = *impl_;
    if (impl.automatedCount < impl.automated.size()) {
        impl.automated.at(impl.automatedCount++) = {.id = id, .value = value};
    }
}

void Vst3Plugin::processBlock(std::span<float* const> mix, core::Frames frames, std::int64_t hostTimeNs,
                              const InstrumentTransport& transport) {
    Impl& impl = *impl_;
    if (frames <= 0 || impl.audioGuard.exchange(true, std::memory_order_acquire)) {
        return;
    }
    for (BusBuffers& bus : impl.inputs) {
        for (std::vector<float>& channel : bus.channels) {
            std::fill_n(channel.data(), std::min(frames, impl.maxFrames), 0.0F);
        }
    }
    if (impl.process(frames, hostTimeNs, transport) && !impl.outputs.empty() &&
        !impl.outputs.front().channels.empty()) {
        const auto& outputs = impl.outputs.front().channels;
        for (std::size_t channel = 0; channel < mix.size(); ++channel) {
            const float* source = outputs[std::min(channel, outputs.size() - 1)].data();
            std::transform(source, source + frames, mix[channel], mix[channel], std::plus{});
        }
    }
    impl.audioGuard.store(false, std::memory_order_release);
}

void Vst3Plugin::processEffect(std::span<float* const> channels, core::Frames frames, std::int64_t hostTimeNs,
                               const InstrumentTransport& transport) {
    Impl& impl = *impl_;
    if (frames <= 0 || channels.empty() || impl.audioGuard.exchange(true, std::memory_order_acquire)) {
        return;
    }
    if (impl.active && frames <= impl.maxFrames && !impl.inputs.empty()) {
        auto& inputs = impl.inputs.front().channels;
        for (std::size_t channel = 0; channel < inputs.size(); ++channel) {
            std::copy_n(channels[std::min(channel, channels.size() - 1)], frames, inputs[channel].data());
        }
    }
    if (impl.process(frames, hostTimeNs, transport) && !impl.outputs.empty() &&
        !impl.outputs.front().channels.empty()) {
        const auto& outputs = impl.outputs.front().channels;
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            std::copy_n(outputs[std::min(channel, outputs.size() - 1)].data(), frames, channels[channel]);
        }
    }
    impl.audioGuard.store(false, std::memory_order_release);
}

std::span<const InstrumentMidi> Vst3Plugin::producedMidi() const { return {}; }

}  // namespace emerick::engine
