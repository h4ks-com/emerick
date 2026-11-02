#include "emerick/app/track_params.hpp"

#include "emerick/engine/effects.hpp"

namespace emerick::app {

namespace {

// Choice params, such as a delay's note value, stay out, because a lane moves a value between points.
void addInsertParams(std::vector<AutomationParam>& params, const model::Insert& insert) {
    const engine::EffectSpec* spec = engine::findEffect(insert.type);
    if (spec == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < spec->params.size(); ++index) {
        const engine::EffectParam& described = spec->params[index];
        if (described.kind == engine::ParamKind::choice) {
            continue;
        }
        const std::string id{described.id};
        const auto value = insert.params.find(id);
        params.push_back({.id = insert.id.value + "." + id,
                          .title = std::string{spec->title} + " " + std::string{described.title},
                          .minimum = described.minimum,
                          .maximum = described.maximum,
                          .value = value != insert.params.end() ? value->second : described.defaultValue,
                          .toggle = described.kind == engine::ParamKind::toggle,
                          .target = core::AutomationTarget::insert,
                          .bus = {},
                          .unit = std::string{described.unit},
                          .insert = insert.id.value,
                          .insertParam = index,
                          .plugin = false,
                          .scale = described.logarithmic ? LaneScale::logarithmic : LaneScale::linear});
    }
}

}  // namespace

std::string sendParamId(const model::TrackId& bus) { return "send." + bus.value + ".level"; }

std::vector<AutomationParam> trackParams(const model::Document& document, const model::Track& track) {
    std::vector<AutomationParam> params{{.id = "gain",
                                         .title = "Volume",
                                         .minimum = kAutomationGainMinDb,
                                         .maximum = kAutomationGainMaxDb,
                                         .value = track.gainDb,
                                         .toggle = false,
                                         .target = core::AutomationTarget::gain,
                                         .bus = {},
                                         .unit = "dB",
                                         .insert = {},
                                         .insertParam = 0,
                                         .plugin = false,
                                         .scale = LaneScale::fader},
                                        {.id = "pan",
                                         .title = "Pan",
                                         .minimum = -1.0,
                                         .maximum = 1.0,
                                         .value = track.pan,
                                         .toggle = false,
                                         .target = core::AutomationTarget::pan,
                                         .bus = {},
                                         .unit = "pan",
                                         .insert = {},
                                         .insertParam = 0},
                                        {.id = "mute",
                                         .title = "Mute",
                                         .minimum = 0.0,
                                         .maximum = 1.0,
                                         .value = track.mute ? 1.0 : 0.0,
                                         .toggle = true,
                                         .target = core::AutomationTarget::mute,
                                         .bus = {},
                                         .unit = "",
                                         .insert = {},
                                         .insertParam = 0}};
    for (const model::Send& send : track.sends) {
        const model::Track* bus = findTrack(document, send.bus);
        params.push_back({.id = sendParamId(send.bus),
                          .title = (bus != nullptr ? bus->name : send.bus.value) + " send",
                          .minimum = kAutomationGainMinDb,
                          .maximum = kAutomationGainMaxDb,
                          .value = send.levelDb,
                          .toggle = false,
                          .target = core::AutomationTarget::send,
                          .bus = send.bus.value,
                          .unit = "dB",
                          .insert = {},
                          .insertParam = 0,
                          .plugin = false,
                          .scale = LaneScale::fader});
    }
    for (const model::Insert& insert : track.inserts) {
        addInsertParams(params, insert);
    }
    return params;
}

}  // namespace emerick::app
