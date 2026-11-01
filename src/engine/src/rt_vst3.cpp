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

void addChange(ParamChanges& changes, vst::ParamID id, vst::ParamValue value, Steinberg::int32 offset = 0) {
    Steinberg::int32 index = 0;
    if (vst::IParamValueQueue* queue = changes.addParameterData(id, index)) {
        queue->addPoint(offset, value, index);
    }
}

constexpr double kSevenBits = 127.0;
constexpr double kFourteenBits = 16383.0;

void addNote(Vst3Plugin::Impl& impl, const QueuedMidi3& message, Steinberg::int32 offset) {
    const std::uint8_t type = message.bytes[0] & 0xF0;
    const auto channel = static_cast<Steinberg::int16>(message.bytes[0] & 0x0F);
    vst::Event event{};
    event.busIndex = 0;
    event.sampleOffset = offset;
    event.flags = vst::Event::kIsLive;
    const auto velocity = static_cast<float>(message.bytes[2] / kSevenBits);
    // vst::Event keeps its payload in a union, and its type says which member is set.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    if (type == core::midi::kNoteOn && message.bytes[2] > 0) {
        event.type = vst::Event::kNoteOnEvent;
        event.noteOn = {.channel = channel,
                        .pitch = message.bytes[1],
                        .tuning = 0.0F,
                        .velocity = velocity,
                        .length = 0,
                        .noteId = -1};
    } else {
        event.type = vst::Event::kNoteOffEvent;
        event.noteOff = {
            .channel = channel, .pitch = message.bytes[1], .velocity = velocity, .noteId = -1, .tuning = 0.0F};
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    impl.events.addEvent(event);
}

// Controllers, pitch bend, channel pressure and program change reach the params the plugin maps them to.
void addController(Vst3Plugin::Impl& impl, const QueuedMidi3& message, Steinberg::int32 offset) {
    const std::uint8_t type = message.bytes[0] & 0xF0;
    const auto& targets = impl.midiTargets.at(message.bytes[0] & 0x0F);
    std::size_t number = 0;
    double value = 0.0;
    switch (type) {
        case 0xB0:
            number = message.bytes[1];
            value = message.bytes[2] / kSevenBits;
            break;
        case 0xE0:
            number = vst::kPitchBend;
            value = (message.bytes[1] | (message.bytes[2] << 7)) / kFourteenBits;
            break;
        case 0xD0:
            number = vst::kAfterTouch;
            value = message.bytes[1] / kSevenBits;
            break;
        case 0xC0:
            number = vst::kCtrlProgramChange;
            break;
        default:
            return;
    }
    const MidiTarget& target = targets.at(number);
    if (target.id == vst::kNoParamId) {
        return;
    }
    if (type == 0xC0) {
        value = target.steps > 0 ? std::min<double>(message.bytes[1], target.steps) / target.steps
                                 : message.bytes[1] / kSevenBits;
    }
    addChange(impl.inputChanges, target.id, std::clamp(value, 0.0, 1.0), offset);
}

void addMidi(Vst3Plugin::Impl& impl, core::Frames frames, std::int64_t hostTimeNs) {
    impl.events.clear();
    for (const QueuedMidi3& message : std::span(impl.notes).first(impl.noteCount)) {
        const auto offset = static_cast<Steinberg::int32>(
            std::clamp<core::Frames>(nsToFrames(message.timestampNs - hostTimeNs, impl.sampleRate), 0, frames - 1));
        const std::uint8_t type = message.bytes[0] & 0xF0;
        if (type == core::midi::kNoteOn || type == core::midi::kNoteOff) {
            addNote(impl, message, offset);
        } else {
            addController(impl, message, offset);
        }
    }
    impl.noteCount = 0;
}

std::uint8_t velocityByte(float velocity, bool on) {
    const auto scaled = static_cast<std::uint8_t>(std::clamp(std::lround(velocity * kSevenBits), 0L, 127L));
    return on ? std::max<std::uint8_t>(scaled, 1) : scaled;
}

// The plugin's notes come out as MIDI note messages, so a track that takes them records and plays them as MIDI.
void collectProduced(Vst3Plugin::Impl& impl) {
    impl.producedCount = 0;
    for (Steinberg::int32 index = 0; index < impl.outputEvents.getEventCount(); ++index) {
        vst::Event event{};
        if (impl.outputEvents.getEvent(index, event) != Steinberg::kResultOk ||
            (event.type != vst::Event::kNoteOnEvent && event.type != vst::Event::kNoteOffEvent) ||
            impl.producedCount == impl.produced.size()) {
            continue;
        }
        const bool on = event.type == vst::Event::kNoteOnEvent;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        const Steinberg::int16 channel = on ? event.noteOn.channel : event.noteOff.channel;
        const Steinberg::int16 pitch = on ? event.noteOn.pitch : event.noteOff.pitch;
        const float velocity = on ? event.noteOn.velocity : event.noteOff.velocity;
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        if (pitch < 0 || pitch > 127) {
            continue;
        }
        auto& bytes = impl.producedBytes.at(impl.producedCount);
        bytes = {static_cast<std::uint8_t>((on ? core::midi::kNoteOn : core::midi::kNoteOff) | (channel & 0x0F)),
                 static_cast<std::uint8_t>(pitch), velocityByte(velocity, on)};
        impl.produced.at(impl.producedCount++) = {.offset = event.sampleOffset, .bytes = bytes};
    }
    impl.outputEvents.clear();
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
        addChange(inputChanges, change.id, change.value);
        ++sent;
    }
    for (const Vst3ParamChange& automatedChange : std::span(automated).first(automatedCount)) {
        addChange(inputChanges, automatedChange.id, automatedChange.value);
    }
    automatedCount = 0;
    if (frames > 0) {
        addMidi(*this, frames, hostTimeNs);
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
    data.outputEvents = sendsNotes ? &outputEvents : nullptr;
    data.processContext = &context;
    const bool played = processor->process(data) == Steinberg::kResultOk;
    collectProduced(*this);
    if (!outputChanges.empty()) {
        paramsChanged.store(true, std::memory_order_relaxed);
    }
    events.clear();
    paramsApplied.fetch_add(sent, std::memory_order_release);
    return played;
}

void Vst3Plugin::queueMidi(std::span<const std::uint8_t> bytes, std::int64_t timestampNs) {
    Impl& impl = *impl_;
    if (bytes.empty() || bytes.size() > 3 || !core::midi::isChannelMessage(bytes[0]) ||
        bytes.size() != core::midi::channelMessageSize(bytes[0]) || impl.noteCount == impl.notes.size()) {
        return;
    }
    QueuedMidi3& message = impl.notes.at(impl.noteCount++);
    message = {.timestampNs = timestampNs, .bytes = {}};
    std::ranges::copy(bytes, message.bytes.begin());
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
    impl.producedCount = 0;
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

std::span<const InstrumentMidi> Vst3Plugin::producedMidi() const {
    return std::span(impl_->produced).first(impl_->producedCount);
}

}  // namespace emerick::engine
