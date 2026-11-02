#include "emerick/app/monitor_actions.hpp"

#include <string>

#include "emerick/app/core_actions.hpp"
#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

constexpr const char* kCategory = "Monitors";
constexpr double kLowestReferenceHz = 430.0;
constexpr double kHighestReferenceHz = 450.0;
constexpr double kDefaultReferenceHz = 440.0;

Json sourceSchema() { return {{"properties", {{"source", {{"type", "string"}}}}}}; }

std::string sourceParam(const Json& params) {
    return params.contains("source") ? param<std::string>(params, "source") : std::string{};
}

}  // namespace

std::unique_ptr<Monitors> registerMonitors(Registry& registry, ProjectController& controller, Nav& nav) {
    addSettingsSection(registry, nav, {.key = "monitors", .title = "Tuner and spectrogram"});
    registry.addSetting({.id = kTunerReferenceSetting,
                         .title = "Tuner reference pitch",
                         .aliases = {"concert pitch", "a4", "calibration"},
                         .category = "Settings / Tuner and spectrogram",
                         .type = SettingType::Number,
                         .defaultValue = kDefaultReferenceHz,
                         .minimum = kLowestReferenceHz,
                         .maximum = kHighestReferenceHz,
                         .hint = "The pitch of A4 in Hz the tuner measures against",
                         .location = "settings.monitors"});
    registerSpectrogramSettings(registry);
    auto monitors = std::make_unique<Monitors>(registry, controller, nav);
    Tuner& tuner = monitors->tuner;
    Spectrogram& spectrogram = monitors->spectrogram;
    registry.addStateProvider("tuner", [&tuner] { return tuner.state(); });
    registry.addStateProvider("spectrogram", [&spectrogram] { return spectrogram.state(); });

    registry.addAction({.id = "view.toggleTuner",
                        .title = "Show or hide tuner",
                        .aliases = {"tuner", "tune guitar", "pitch meter"},
                        .category = "View",
                        .run =
                            [&nav, &tuner](const Json&) {
                                nav.setTunerOpen(!nav.tunerOpen());
                                return tuner.state();
                            },
                        .menus = {kViewMenu}});
    registry.addAction({.id = "view.toggleSpectrogram",
                        .title = "Show or hide spectrogram",
                        .aliases = {"spectrogram", "spectrum analyzer", "waterfall", "frequency analyzer"},
                        .category = "View",
                        .run =
                            [&nav, &spectrogram](const Json&) {
                                nav.setSpectrogramOpen(!nav.spectrogramOpen());
                                return spectrogram.state();
                            },
                        .menus = {kViewMenu}});
    registry.addAction({.id = "tuner.setSource",
                        .title = "Set tuner source",
                        .description = "Listens to source: a track id, master, or empty for the selected track. An "
                                       "armed or monitored audio track is heard at its live input",
                        .aliases = {"tune track", "tuner input"},
                        .category = kCategory,
                        .schema = sourceSchema(),
                        .run = [&tuner](const Json& params) {
                            tuner.setSource(sourceParam(params));
                            return tuner.state();
                        }});
    registry.addAction({.id = "spectrogram.setSource",
                        .title = "Set spectrogram source",
                        .description = "Listens to source: a track id, master, or empty for the master",
                        .aliases = {"analyze track", "spectrogram input"},
                        .category = kCategory,
                        .schema = sourceSchema(),
                        .run = [&spectrogram](const Json& params) {
                            spectrogram.setSource(sourceParam(params));
                            return spectrogram.state();
                        }});
    return monitors;
}

}  // namespace emerick::app
