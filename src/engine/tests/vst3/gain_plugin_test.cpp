// A VST3 effect for the host tests: it scales its stereo input by Gain and can invert it with Polarity, in one object
// that is component, processor and controller. Built with EMERICK_TEST_CRASH or EMERICK_TEST_HANG, its module entry
// crashes or hangs, as a broken plugin would.

#include <pluginterfaces/base/funknownimpl.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vstspeaker.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <thread>

namespace {

namespace sb = Steinberg;
namespace vst = Steinberg::Vst;

enum ParamId : vst::ParamID { kGain = 1, kPolarity = 2, kPeak = 3 };

constexpr std::array<std::u16string_view, 2> kPolarityNames{u"Normal", u"Inverted"};

// The class id the tests load the effect by, as UID::toString prints it.
const sb::FUID kEffectId(0x454D5254, 0x47414E31, 0x00000000, 0x00000001);

void copyText(std::u16string_view text, vst::String128 out) {
    const std::size_t count = std::min<std::size_t>(text.size(), 127);
    std::ranges::copy(text.substr(0, count), out);
    out[count] = 0;
}

std::u16string widen(const std::string& text) { return {text.begin(), text.end()}; }

class GainEffect final
    : public sb::U::Implements<sb::U::Directly<vst::IComponent, vst::IAudioProcessor, vst::IEditController>> {
public:
    sb::tresult PLUGIN_API initialize(sb::FUnknown* /*context*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API terminate() override { return sb::kResultOk; }

    sb::tresult PLUGIN_API getControllerClassId(sb::TUID /*classId*/) override { return sb::kNotImplemented; }
    sb::tresult PLUGIN_API setIoMode(vst::IoMode /*mode*/) override { return sb::kResultOk; }
    sb::int32 PLUGIN_API getBusCount(vst::MediaType type, vst::BusDirection /*dir*/) override {
        return type == vst::kAudio ? 1 : 0;
    }
    sb::tresult PLUGIN_API getBusInfo(vst::MediaType type, vst::BusDirection dir, sb::int32 index,
                                      vst::BusInfo& bus) override {
        if (type != vst::kAudio || index != 0) {
            return sb::kInvalidArgument;
        }
        bus.mediaType = type;
        bus.direction = dir;
        bus.channelCount = 2;
        copyText(dir == vst::kInput ? u"Input" : u"Output", bus.name);
        bus.busType = vst::kMain;
        bus.flags = vst::BusInfo::kDefaultActive;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getRoutingInfo(vst::RoutingInfo& /*in*/, vst::RoutingInfo& /*out*/) override {
        return sb::kNotImplemented;
    }
    sb::tresult PLUGIN_API activateBus(vst::MediaType /*type*/, vst::BusDirection /*dir*/, sb::int32 /*index*/,
                                       sb::TBool /*state*/) override {
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API setActive(sb::TBool /*state*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API setState(sb::IBStream* state) override {
        std::array<double, 2> values{};
        sb::int32 read = 0;
        if (state == nullptr || state->read(values.data(), sizeof(values), &read) != sb::kResultOk ||
            read != sizeof(values)) {
            return sb::kResultFalse;
        }
        gain_ = values[0];
        polarity_ = values[1];
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getState(sb::IBStream* state) override {
        std::array<double, 2> values{gain_, polarity_};
        return state->write(values.data(), sizeof(values), nullptr);
    }

    sb::tresult PLUGIN_API setBusArrangements(vst::SpeakerArrangement* /*inputs*/, sb::int32 /*numIns*/,
                                              vst::SpeakerArrangement* /*outputs*/, sb::int32 /*numOuts*/) override {
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getBusArrangement(vst::BusDirection /*dir*/, sb::int32 /*index*/,
                                             vst::SpeakerArrangement& arrangement) override {
        arrangement = vst::SpeakerArr::kStereo;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API canProcessSampleSize(sb::int32 size) override {
        return size == vst::kSample32 ? sb::kResultOk : sb::kResultFalse;
    }
    sb::uint32 PLUGIN_API getLatencySamples() override { return 0; }
    sb::tresult PLUGIN_API setupProcessing(vst::ProcessSetup& /*setup*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API setProcessing(sb::TBool /*state*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API process(vst::ProcessData& data) override {
        readChanges(data.inputParameterChanges);
        if (data.numSamples == 0 || data.numInputs == 0 || data.numOutputs == 0) {
            return sb::kResultOk;
        }
        const auto frames = static_cast<std::size_t>(data.numSamples);
        const float scale = static_cast<float>(gain_ * 2.0 * (polarity_ >= 0.5 ? -1.0 : 1.0));
        for (sb::int32 channel = 0; channel < data.outputs[0].numChannels; ++channel) {
            const std::span in(data.inputs[0].channelBuffers32[std::min(channel, data.inputs[0].numChannels - 1)],
                               frames);
            const std::span out(data.outputs[0].channelBuffers32[channel], frames);
            std::ranges::transform(in, out.begin(), [scale](float sample) { return sample * scale; });
        }
        reportPeak(data);
        return sb::kResultOk;
    }
    sb::uint32 PLUGIN_API getTailSamples() override { return 0; }

    sb::tresult PLUGIN_API setComponentState(sb::IBStream* state) override { return setState(state); }
    sb::int32 PLUGIN_API getParameterCount() override { return 3; }
    sb::tresult PLUGIN_API getParameterInfo(sb::int32 index, vst::ParameterInfo& info) override {
        if (index < 0 || index > 2) {
            return sb::kInvalidArgument;
        }
        info = {};
        info.id = static_cast<vst::ParamID>(index + 1);
        copyText(std::array{u"Gain", u"Polarity", u"Peak"}.at(static_cast<std::size_t>(index)), info.title);
        info.stepCount = index == 1 ? 1 : 0;
        info.defaultNormalizedValue = index == 0 ? 0.5 : 0.0;
        info.flags = index == 2 ? vst::ParameterInfo::kIsReadOnly
                                : vst::ParameterInfo::kCanAutomate | (index == 1 ? vst::ParameterInfo::kIsList : 0);
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getParamStringByValue(vst::ParamID id, vst::ParamValue value, vst::String128 text) override {
        copyText(id == kPolarity ? kPolarityNames.at(value >= 0.5 ? 1 : 0)
                                 : std::u16string_view{widen(std::format("{:.2f}x", value * 2.0))},
                 text);
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getParamValueByString(vst::ParamID /*id*/, vst::TChar* /*text*/,
                                                 vst::ParamValue& /*value*/) override {
        return sb::kNotImplemented;
    }
    vst::ParamValue PLUGIN_API normalizedParamToPlain(vst::ParamID id, vst::ParamValue value) override {
        return id == kGain ? value * 2.0 : value;
    }
    vst::ParamValue PLUGIN_API plainParamToNormalized(vst::ParamID id, vst::ParamValue value) override {
        return id == kGain ? value / 2.0 : value;
    }
    vst::ParamValue PLUGIN_API getParamNormalized(vst::ParamID id) override {
        return id == kGain ? gain_ : id == kPolarity ? polarity_ : shownPeak_;
    }
    // The processor takes Gain and Polarity from its param changes and shares them with the controller, which shows
    // Peak as the host hands it over.
    sb::tresult PLUGIN_API setParamNormalized(vst::ParamID id, vst::ParamValue value) override {
        if (id == kPeak) {
            shownPeak_ = value;
        }
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API setComponentHandler(vst::IComponentHandler* handler) override {
        handler_ = handler;
        return sb::kResultOk;
    }
    sb::IPlugView* PLUGIN_API createView(sb::FIDString name) override;

    // What a user turning the Gain knob to 0.25 in the editor does.
    void turnGainInEditor() {
        if (handler_ == nullptr) {
            return;
        }
        handler_->beginEdit(kGain);
        gain_ = 0.125;
        handler_->performEdit(kGain, gain_);
        handler_->endEdit(kGain);
    }

private:
    vst::IComponentHandler* handler_ = nullptr;
    void readChanges(vst::IParameterChanges* changes) {
        if (changes == nullptr) {
            return;
        }
        for (sb::int32 index = 0; index < changes->getParameterCount(); ++index) {
            vst::IParamValueQueue* queue = changes->getParameterData(index);
            sb::int32 offset = 0;
            vst::ParamValue value = 0.0;
            if (queue == nullptr || queue->getPoint(queue->getPointCount() - 1, offset, value) != sb::kResultOk) {
                continue;
            }
            (queue->getParameterId() == kGain ? gain_ : polarity_) = value;
        }
    }

    double gain_ = 0.5;
    double polarity_ = 0.0;
    double shownPeak_ = 0.0;

    // The processor reports the loudest output sample of the block as Peak.
    static void reportPeak(vst::ProcessData& data) {
        if (data.outputParameterChanges == nullptr) {
            return;
        }
        float peak = 0.0F;
        for (sb::int32 channel = 0; channel < data.outputs[0].numChannels; ++channel) {
            for (const float sample :
                 std::span(data.outputs[0].channelBuffers32[channel], static_cast<std::size_t>(data.numSamples))) {
                peak = std::max(peak, std::abs(sample));
            }
        }
        sb::int32 index = 0;
        if (vst::IParamValueQueue* queue = data.outputParameterChanges->addParameterData(kPeak, index)) {
            queue->addPoint(0, std::min(peak, 1.0F), index);
        }
    }
};

// The editor draws nothing and keeps its parent untouched, so it attaches to any parent offscreen. As it opens it
// asks to grow to 400 by 250 and turns the Gain knob, so the tests see a resize request and an edit from the editor.
class GainView final : public sb::U::Implements<sb::U::Directly<sb::IPlugView>> {
public:
    explicit GainView(GainEffect& effect) : effect_(&effect) {}

    sb::tresult PLUGIN_API isPlatformTypeSupported(sb::FIDString /*type*/) override { return sb::kResultTrue; }
    sb::tresult PLUGIN_API attached(void* parent, sb::FIDString /*type*/) override {
        if (parent == nullptr) {
            return sb::kResultFalse;
        }
        sb::ViewRect grown{0, 0, 400, 250};
        if (frame_ != nullptr) {
            frame_->resizeView(this, &grown);
        }
        effect_->turnGainInEditor();
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API removed() override { return sb::kResultOk; }
    sb::tresult PLUGIN_API onWheel(float /*distance*/) override { return sb::kResultFalse; }
    sb::tresult PLUGIN_API onKeyDown(sb::char16 /*key*/, sb::int16 /*code*/, sb::int16 /*modifiers*/) override {
        return sb::kResultFalse;
    }
    sb::tresult PLUGIN_API onKeyUp(sb::char16 /*key*/, sb::int16 /*code*/, sb::int16 /*modifiers*/) override {
        return sb::kResultFalse;
    }
    sb::tresult PLUGIN_API getSize(sb::ViewRect* size) override {
        *size = rect_;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API onSize(sb::ViewRect* size) override {
        rect_ = *size;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API onFocus(sb::TBool /*state*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API setFrame(sb::IPlugFrame* frame) override {
        frame_ = frame;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API canResize() override { return sb::kResultTrue; }
    sb::tresult PLUGIN_API checkSizeConstraint(sb::ViewRect* rect) override {
        rect->right = rect->left + std::max(rect->getWidth(), 100);
        rect->bottom = rect->top + std::max(rect->getHeight(), 100);
        return sb::kResultOk;
    }

private:
    GainEffect* effect_;
    sb::IPlugFrame* frame_ = nullptr;
    sb::ViewRect rect_{0, 0, 300, 200};
};

sb::IPlugView* PLUGIN_API GainEffect::createView(sb::FIDString name) {
    return std::string_view{name} == vst::ViewType::kEditor ? new GainView(*this) : nullptr;
}

class Factory final
    : public sb::U::Implements<sb::U::Directly<sb::IPluginFactory2>, sb::U::Indirectly<sb::IPluginFactory>> {
public:
    sb::tresult PLUGIN_API getFactoryInfo(sb::PFactoryInfo* info) override {
        *info = sb::PFactoryInfo("Emerick Tests", "", "", sb::PFactoryInfo::kUnicode);
        return sb::kResultOk;
    }
    sb::int32 PLUGIN_API countClasses() override { return 1; }
    sb::tresult PLUGIN_API getClassInfo(sb::int32 index, sb::PClassInfo* info) override {
        if (index != 0) {
            return sb::kInvalidArgument;
        }
        *info = sb::PClassInfo(kEffectId.toTUID(), sb::PClassInfo::kManyInstances, kVstAudioEffectClass, "Test Gain");
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getClassInfo2(sb::int32 index, sb::PClassInfo2* info) override {
        if (index != 0) {
            return sb::kInvalidArgument;
        }
        *info = sb::PClassInfo2(kEffectId.toTUID(), sb::PClassInfo::kManyInstances, kVstAudioEffectClass, "Test Gain",
                                0, "Fx|Dynamics", "Emerick Tests", "1.0.0", kVstVersionString);
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API createInstance(sb::FIDString cid, sb::FIDString iid, void** object) override {
        if (!sb::FUnknownPrivate::iidEqual(cid, kEffectId.toTUID())) {
            *object = nullptr;
            return sb::kNoInterface;
        }
        // VST3 hands out reference counted objects across a C boundary, so the plugin creates them with new.
        auto* effect = new GainEffect;
        const sb::tresult result = effect->queryInterface(iid, object);
        effect->release();
        return result;
    }
};

bool enterModule() {
#if defined(EMERICK_TEST_CRASH)
    std::abort();
#elif defined(EMERICK_TEST_HANG)
    std::this_thread::sleep_for(std::chrono::hours(1));
#endif
    return true;
}

}  // namespace

// The host finds these by name.
// NOLINTBEGIN(readability-identifier-naming)
extern "C" {
__attribute__((visibility("default"))) bool bundleEntry(void* /*bundle*/) { return enterModule(); }
__attribute__((visibility("default"))) bool bundleExit() { return true; }
__attribute__((visibility("default"))) bool ModuleEntry(void* /*library*/) { return enterModule(); }
__attribute__((visibility("default"))) bool ModuleExit() { return true; }
__attribute__((visibility("default"))) sb::IPluginFactory* PLUGIN_API GetPluginFactory() {
    static Factory factory;
    factory.addRef();
    return &factory;
}
}
// NOLINTEND(readability-identifier-naming)
