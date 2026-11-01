// A VST3 instrument for the host tests: one sine voice at the last note's pitch and velocity, scaled by Level, bent
// up to two semitones either way by Bend, with a Program param. It maps controller 7 to Level, pitch bend to Bend and
// program change to Program, and sends every note it gets back out on its event output. Its editor turns Level as
// it opens.

#include <pluginterfaces/base/funknownimpl.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vstspeaker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <string>

namespace {

namespace sb = Steinberg;
namespace vst = Steinberg::Vst;

enum ParamId : vst::ParamID { kLevel = 1, kBend = 2, kProgram = 3 };

constexpr std::array<std::u16string_view, 4> kPrograms{u"Pure", u"Soft", u"Bright", u"Dark"};

const sb::FUID kSynthId(0x454D5254, 0x53594E31, 0x00000000, 0x00000001);

void copyText(std::u16string_view text, vst::String128 out) {
    const std::size_t count = std::min<std::size_t>(text.size(), 127);
    std::ranges::copy(text.substr(0, count), out);
    out[count] = 0;
}

std::u16string widen(const std::string& text) { return {text.begin(), text.end()}; }

class SineSynth final
    : public sb::U::Implements<
          sb::U::Directly<vst::IComponent, vst::IAudioProcessor, vst::IEditController, vst::IMidiMapping>> {
public:
    sb::tresult PLUGIN_API initialize(sb::FUnknown* /*context*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API terminate() override { return sb::kResultOk; }

    sb::tresult PLUGIN_API getControllerClassId(sb::TUID /*classId*/) override { return sb::kNotImplemented; }
    sb::tresult PLUGIN_API setIoMode(vst::IoMode /*mode*/) override { return sb::kResultOk; }
    sb::int32 PLUGIN_API getBusCount(vst::MediaType type, vst::BusDirection dir) override {
        return type == vst::kEvent || dir == vst::kOutput ? 1 : 0;
    }
    sb::tresult PLUGIN_API getBusInfo(vst::MediaType type, vst::BusDirection dir, sb::int32 index,
                                      vst::BusInfo& bus) override {
        if (index != 0 || (type == vst::kAudio && dir == vst::kInput)) {
            return sb::kInvalidArgument;
        }
        bus.mediaType = type;
        bus.direction = dir;
        bus.channelCount = type == vst::kAudio ? 2 : 16;
        copyText(type == vst::kAudio ? u"Output" : u"MIDI", bus.name);
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
        std::array<double, 3> values{};
        sb::int32 read = 0;
        if (state == nullptr || state->read(values.data(), sizeof(values), &read) != sb::kResultOk ||
            read != sizeof(values)) {
            return sb::kResultFalse;
        }
        level_ = values[0];
        bend_ = values[1];
        program_ = values[2];
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getState(sb::IBStream* state) override {
        std::array<double, 3> values{level_, bend_, program_};
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
    sb::tresult PLUGIN_API setupProcessing(vst::ProcessSetup& setup) override {
        rate_ = setup.sampleRate;
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API setProcessing(sb::TBool /*state*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API process(vst::ProcessData& data) override {
        readChanges(data.inputParameterChanges);
        readNotes(data);
        if (data.numSamples == 0 || data.numOutputs == 0) {
            return sb::kResultOk;
        }
        const auto frames = static_cast<std::size_t>(data.numSamples);
        const double pitch = pitch_ + ((bend_ - 0.5) * 4.0);
        const double step = 2.0 * std::numbers::pi * 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0) / rate_;
        const auto amplitude = static_cast<float>(sounding_ ? level_ * velocity_ : 0.0);
        const std::span left(data.outputs[0].channelBuffers32[0], frames);
        for (float& sample : left) {
            sample = amplitude * static_cast<float>(std::sin(phase_));
            phase_ += step;
        }
        for (sb::int32 channel = 1; channel < data.outputs[0].numChannels; ++channel) {
            std::ranges::copy(left, data.outputs[0].channelBuffers32[channel]);
        }
        return sb::kResultOk;
    }
    sb::uint32 PLUGIN_API getTailSamples() override { return 0; }

    sb::tresult PLUGIN_API getMidiControllerAssignment(sb::int32 /*bus*/, sb::int16 /*channel*/, vst::CtrlNumber number,
                                                       vst::ParamID& id) override {
        switch (number) {
            case vst::kCtrlVolume:
                id = kLevel;
                return sb::kResultTrue;
            case vst::kPitchBend:
                id = kBend;
                return sb::kResultTrue;
            case vst::kCtrlProgramChange:
                id = kProgram;
                return sb::kResultTrue;
            default:
                return sb::kResultFalse;
        }
    }

    sb::tresult PLUGIN_API setComponentState(sb::IBStream* state) override { return setState(state); }
    sb::int32 PLUGIN_API getParameterCount() override { return 3; }
    sb::tresult PLUGIN_API getParameterInfo(sb::int32 index, vst::ParameterInfo& info) override {
        if (index < 0 || index > 2) {
            return sb::kInvalidArgument;
        }
        info = {};
        info.id = static_cast<vst::ParamID>(index + 1);
        copyText(std::array{u"Level", u"Bend", u"Program"}.at(static_cast<std::size_t>(index)), info.title);
        info.stepCount = index == 2 ? 3 : 0;
        info.defaultNormalizedValue = index == 2 ? 0.0 : 0.5;
        info.flags = vst::ParameterInfo::kCanAutomate | (index == 2 ? vst::ParameterInfo::kIsList : 0);
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getParamStringByValue(vst::ParamID id, vst::ParamValue value, vst::String128 text) override {
        if (id == kProgram) {
            copyText(kPrograms.at(static_cast<std::size_t>(std::lround(value * 3.0))), text);
        } else {
            copyText(widen(std::format("{:.2f}", value)), text);
        }
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getParamValueByString(vst::ParamID /*id*/, vst::TChar* /*text*/,
                                                 vst::ParamValue& /*value*/) override {
        return sb::kNotImplemented;
    }
    vst::ParamValue PLUGIN_API normalizedParamToPlain(vst::ParamID /*id*/, vst::ParamValue value) override {
        return value;
    }
    vst::ParamValue PLUGIN_API plainParamToNormalized(vst::ParamID /*id*/, vst::ParamValue value) override {
        return value;
    }
    vst::ParamValue PLUGIN_API getParamNormalized(vst::ParamID id) override {
        return id == kLevel ? level_ : id == kBend ? bend_ : program_;
    }
    sb::tresult PLUGIN_API setParamNormalized(vst::ParamID /*id*/, vst::ParamValue /*value*/) override {
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API setComponentHandler(vst::IComponentHandler* handler) override {
        handler_ = handler;
        return sb::kResultOk;
    }
    sb::IPlugView* PLUGIN_API createView(sb::FIDString name) override;

    // What a user turning the Level knob to 0.25 in the editor does.
    void turnLevelInEditor() {
        if (handler_ == nullptr) {
            return;
        }
        handler_->beginEdit(kLevel);
        level_ = 0.25;
        handler_->performEdit(kLevel, level_);
        handler_->endEdit(kLevel);
    }

private:
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
            const vst::ParamID id = queue->getParameterId();
            (id == kLevel ? level_ : id == kBend ? bend_ : program_) = value;
        }
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    void readNotes(vst::ProcessData& data) {
        if (data.inputEvents == nullptr) {
            return;
        }
        for (sb::int32 index = 0; index < data.inputEvents->getEventCount(); ++index) {
            vst::Event event{};
            if (data.inputEvents->getEvent(index, event) != sb::kResultOk) {
                continue;
            }
            if (event.type == vst::Event::kNoteOnEvent) {
                sounding_ = true;
                pitch_ = event.noteOn.pitch;
                velocity_ = event.noteOn.velocity;
            } else if (event.type == vst::Event::kNoteOffEvent && event.noteOff.pitch == pitch_) {
                sounding_ = false;
            } else {
                continue;
            }
            if (data.outputEvents != nullptr) {
                data.outputEvents->addEvent(event);
            }
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    double level_ = 0.5;
    double bend_ = 0.5;
    double program_ = 0.0;
    double rate_ = 48000.0;
    double phase_ = 0.0;
    bool sounding_ = false;
    int pitch_ = 69;
    double velocity_ = 1.0;
    vst::IComponentHandler* handler_ = nullptr;
};

// The editor draws nothing and keeps its parent untouched, so it attaches to any parent offscreen. As it opens it turns
// the Level knob, so the tests see an edit from the editor.
class SynthView final : public sb::U::Implements<sb::U::Directly<sb::IPlugView>> {
public:
    explicit SynthView(SineSynth& synth) : synth_(&synth) {}

    sb::tresult PLUGIN_API isPlatformTypeSupported(sb::FIDString /*type*/) override { return sb::kResultTrue; }
    sb::tresult PLUGIN_API attached(void* parent, sb::FIDString /*type*/) override {
        if (parent == nullptr) {
            return sb::kResultFalse;
        }
        synth_->turnLevelInEditor();
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
        *size = {0, 0, 320, 180};
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API onSize(sb::ViewRect* /*size*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API onFocus(sb::TBool /*state*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API setFrame(sb::IPlugFrame* /*frame*/) override { return sb::kResultOk; }
    sb::tresult PLUGIN_API canResize() override { return sb::kResultFalse; }
    sb::tresult PLUGIN_API checkSizeConstraint(sb::ViewRect* /*rect*/) override { return sb::kResultOk; }

private:
    SineSynth* synth_;
};

sb::IPlugView* PLUGIN_API SineSynth::createView(sb::FIDString name) {
    return std::string_view{name} == vst::ViewType::kEditor ? new SynthView(*this) : nullptr;
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
        *info = sb::PClassInfo(kSynthId.toTUID(), sb::PClassInfo::kManyInstances, kVstAudioEffectClass, "Test Synth");
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API getClassInfo2(sb::int32 index, sb::PClassInfo2* info) override {
        if (index != 0) {
            return sb::kInvalidArgument;
        }
        *info = sb::PClassInfo2(kSynthId.toTUID(), sb::PClassInfo::kManyInstances, kVstAudioEffectClass, "Test Synth",
                                0, "Instrument|Synth", "Emerick Tests", "1.0.0", kVstVersionString);
        return sb::kResultOk;
    }
    sb::tresult PLUGIN_API createInstance(sb::FIDString cid, sb::FIDString iid, void** object) override {
        if (!sb::FUnknownPrivate::iidEqual(cid, kSynthId.toTUID())) {
            *object = nullptr;
            return sb::kNoInterface;
        }
        // VST3 hands out reference counted objects across a C boundary, so the plugin creates them with new.
        auto* synth = new SineSynth;
        const sb::tresult result = synth->queryInterface(iid, object);
        synth->release();
        return result;
    }
};

}  // namespace

// The host finds these by name.
// NOLINTBEGIN(readability-identifier-naming)
extern "C" {
__attribute__((visibility("default"))) bool bundleEntry(void* /*bundle*/) { return true; }
__attribute__((visibility("default"))) bool bundleExit() { return true; }
__attribute__((visibility("default"))) bool ModuleEntry(void* /*library*/) { return true; }
__attribute__((visibility("default"))) bool ModuleExit() { return true; }
__attribute__((visibility("default"))) sb::IPluginFactory* PLUGIN_API GetPluginFactory() {
    static Factory factory;
    factory.addRef();
    return &factory;
}
}
// NOLINTEND(readability-identifier-naming)
