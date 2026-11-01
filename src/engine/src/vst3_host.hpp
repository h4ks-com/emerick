#pragma once

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "audio_guard.hpp"
#include "spsc_ring.hpp"
#include "vst3_plugin.hpp"

namespace emerick::engine {

namespace vst = Steinberg::Vst;

inline constexpr std::size_t kMaxVst3Params = 64;
inline constexpr std::size_t kMaxVst3ParamPoints = 16;
inline constexpr std::size_t kMaxVst3Notes = 1024;

// A host object the plugin borrows: we own it, so counting its references does nothing.
template <class Interface>
class Unowned : public Interface {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** object) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, std::data(Steinberg::FUnknown::iid.toTUID())) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, std::data(Interface::iid.toTUID()))) {
            *object = static_cast<Interface*>(this);
            return Steinberg::kResultOk;
        }
        *object = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

protected:
    Unowned() = default;
    ~Unowned() = default;

public:
    Unowned(const Unowned&) = delete;
    Unowned& operator=(const Unowned&) = delete;
    Unowned(Unowned&&) = delete;
    Unowned& operator=(Unowned&&) = delete;
};

// A fixed number of points, so neither we nor the plugin allocate on the audio thread.
class ParamQueue final : public Unowned<vst::IParamValueQueue> {
public:
    void reset(vst::ParamID id) {
        id_ = id;
        count_ = 0;
    }
    vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return static_cast<Steinberg::int32>(count_); }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                           vst::ParamValue& value) override;
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, vst::ParamValue value,
                                           Steinberg::int32& index) override;

private:
    struct Point {
        Steinberg::int32 offset = 0;
        vst::ParamValue value = 0.0;
    };
    vst::ParamID id_ = vst::kNoParamId;
    std::array<Point, kMaxVst3ParamPoints> points_{};
    std::size_t count_ = 0;
};

class ParamChanges final : public Unowned<vst::IParameterChanges> {
public:
    void clear() { used_ = 0; }
    [[nodiscard]] bool empty() const { return used_ == 0; }
    Steinberg::int32 PLUGIN_API getParameterCount() override { return static_cast<Steinberg::int32>(used_); }
    vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override;
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID& id, Steinberg::int32& index) override;

private:
    std::array<ParamQueue, kMaxVst3Params> queues_{};
    std::size_t used_ = 0;
};

class EventList final : public Unowned<vst::IEventList> {
public:
    void clear() { count_ = 0; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return static_cast<Steinberg::int32>(count_); }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, vst::Event& event) override;
    Steinberg::tresult PLUGIN_API addEvent(vst::Event& event) override;

private:
    std::array<vst::Event, kMaxVst3Notes> events_{};
    std::size_t count_ = 0;
};

// The controller reports edits from its own editor and asks for restarts here, from the main thread. An edit
// between beginEdit and endEdit is one gesture, and its end marks the state changed.
class ComponentHandler final : public Unowned<vst::IComponentHandler> {
public:
    explicit ComponentHandler(Vst3Plugin::Impl& impl) : impl_(&impl) {}
    Steinberg::tresult PLUGIN_API beginEdit(vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(vst::ParamID id, vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API endEdit(vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

private:
    Vst3Plugin::Impl* impl_;
    int openEdits_ = 0;
};

// The editor asks the host to resize its window here, from the main thread.
class PlugFrame final : public Unowned<Steinberg::IPlugFrame> {
public:
    explicit PlugFrame(Vst3Plugin::Impl& impl) : impl_(&impl) {}
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

private:
    Vst3Plugin::Impl* impl_;
};

struct Vst3ParamChange {
    vst::ParamID id = 0;
    vst::ParamValue value = 0.0;
};

struct QueuedNote {
    std::int64_t timestampNs = 0;
    std::array<std::uint8_t, 3> bytes{};
};

struct BusBuffers {
    std::vector<std::vector<float>> channels;
    std::vector<float*> pointers;
};

struct Vst3Plugin::Impl {
    Impl() = default;
    ~Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    // Main thread, holding the audio guard.
    void deactivate();
    bool activateAt(double rate, core::Frames frames);
    // Main thread, holding the audio guard: runs a process call of no frames, which hands the queued param changes to
    // the processor.
    void flushParams();
    // Main thread: queues an edit for the processor, and flushes the queue first when it is full.
    void sendEdit(vst::ParamID id, vst::ParamValue value);
    // Main thread: returns once the processor has every queued change, flushing them when it is not processing.
    void waitForParams();
    // Holding the audio guard as the audio thread: one process call over the bus buffers.
    bool process(core::Frames frames, std::int64_t hostTimeNs, const InstrumentTransport& transport);
    [[nodiscard]] std::optional<vst::ParameterInfo> info(vst::ParamID id) const;
    [[nodiscard]] vst::ParamValue normalized(vst::ParamID id, double value) const;

    VST3::Hosting::Module::Ptr module;
    vst::HostApplication host;
    Steinberg::IPtr<vst::IComponent> component;
    Steinberg::IPtr<vst::IAudioProcessor> processor;
    Steinberg::IPtr<vst::IEditController> controller;
    // Set when the controller is its own object, which we connect to the component.
    bool separateController = false;
    Steinberg::IPtr<vst::IConnectionPoint> componentConnection;
    Steinberg::IPtr<vst::IConnectionPoint> controllerConnection;
    ComponentHandler handler{*this};
    PlugFrame frame{*this};
    std::string name;

    // Main thread only.
    Steinberg::IPtr<Steinberg::IPlugView> view;
    std::optional<EditorSize> requestedSize;
    // Set while we load a state, so the plugin's own reports of new values mark nothing changed.
    bool loading = false;

    std::atomic<bool> audioGuard{false};
    std::atomic<bool> restartRequested{false};
    std::atomic<bool> paramsChanged{false};
    std::atomic<bool> stateDirty{false};
    // The main thread writes param changes and the holder of the audio guard reads them; each count grows by one per
    // change, so the main thread knows when the processor has every change it sent.
    SpscRing paramQueue{kMaxVst3Params * sizeof(Vst3ParamChange)};
    std::uint64_t paramsSent = 0;
    std::atomic<std::uint64_t> paramsApplied{0};

    // The holder of the audio guard owns these. The main thread may read active without the guard as a hint.
    bool active = false;
    double sampleRate = 0.0;
    core::Frames maxFrames = 0;
    core::Frames latency = 0;
    bool takesNotes = false;
    std::vector<BusBuffers> inputs;
    std::vector<BusBuffers> outputs;
    std::vector<vst::AudioBusBuffers> inputBuses;
    std::vector<vst::AudioBusBuffers> outputBuses;
    ParamChanges inputChanges;
    ParamChanges outputChanges;
    EventList events;

    // Only the audio thread touches these, between its queue calls and the next process call.
    std::array<QueuedNote, kMaxVst3Notes> notes{};
    std::size_t noteCount = 0;
    std::array<Vst3ParamChange, kMaxVst3Params> automated{};
    std::size_t automatedCount = 0;
};

}  // namespace emerick::engine
