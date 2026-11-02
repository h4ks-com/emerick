#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "emerick/app/monitor_actions.hpp"
#include "project_rig.hpp"

using namespace emerick::app;
using namespace emerick::app::test;

namespace {

struct MonitorRig {
    ProjectRig rig;
    std::unique_ptr<Monitors> monitors = registerMonitors(rig.registry, *rig.controller, rig.nav);

    // Feeds a sine to the interface's first input for long enough that every reading settles.
    void playTone(double hz) const {
        constexpr std::size_t kFrames = 48000 * 4;
        std::vector<float> samples(kFrames);
        for (std::size_t i = 0; i < kFrames; ++i) {
            samples[i] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        }
        rig.audio->setInput("interface", 1, std::move(samples));
    }
};

}  // namespace

TEST_CASE("the monitors register their window toggles, sources and settings with defaults") {
    MonitorRig monitors;
    const Registry& registry = monitors.rig.registry;
    for (const char* id : {"view.toggleTuner", "view.toggleSpectrogram", "tuner.setSource", "spectrogram.setSource"}) {
        CHECK(registry.action(id) != nullptr);
    }
    CHECK(registry.settingValue("tuner.referencePitch") == 440.0);
    CHECK(registry.settingValue("spectrogram.fftSize") == "4096");
    CHECK(registry.settingValue("spectrogram.window") == "Blackman-Harris");
    CHECK(registry.settingValue("spectrogram.floorDb") == -100);
    CHECK(registry.settingValue("spectrogram.ceilingDb") == 0);
    CHECK(registry.settingValue("spectrogram.frequencyAxis") == "Logarithmic");
    const Json spectrogram = registry.state("spectrogram");
    CHECK(spectrogram.at("open") == false);
    CHECK(spectrogram.at("fftSize") == 4096);
    CHECK(spectrogram.at("frequencyAxis") == "log");
    CHECK(registry.state("tuner").at("note").is_null());
}

TEST_CASE("a monitor refuses a source that names no track") {
    MonitorRig monitors;
    CHECK_THROWS_AS(monitors.rig.run("tuner.setSource", {{"source", "nowhere"}}), AppError);
    CHECK_THROWS_AS(monitors.rig.run("spectrogram.setSource", {{"source", "nowhere"}}), AppError);
    CHECK(monitors.rig.run("spectrogram.setSource", {{"source", "master"}}).at("chosenSource") == "master");
}

TEST_CASE("the tuner follows the selected track and hears an armed audio track at its live input") {
    MonitorRig monitors;
    const std::string vox = monitors.rig.run("track.addAudio", {{"name", "Guitar"}}).at("id");
    monitors.rig.run("track.select", {{"track", vox}});
    monitors.rig.run("view.toggleTuner");
    CHECK(monitors.rig.registry.state("tuner").at("source").at("point") == "output");
    monitors.rig.run("track.setArm", {{"track", vox}, {"value", true}});
    QCoreApplication::processEvents();
    const Json source = monitors.rig.registry.state("tuner").at("source");
    CHECK(source.at("track") == vox);
    CHECK(source.at("point") == "input");
}

TEST_CASE("the tuner settles on a tone at the reference pitch and reads its cents") {
    MonitorRig monitors;
    const std::string vox = monitors.rig.run("track.addAudio", {{"name", "Guitar"}}).at("id");
    monitors.rig.run("track.setArm", {{"track", vox}, {"value", true}});
    monitors.rig.run("tuner.setSource", {{"source", vox}});
    monitors.rig.run("view.toggleTuner");
    monitors.playTone(110.0 * std::pow(2.0, 12.0 / 1200));
    CHECK(processUntil([&] {
        const Json tuner = monitors.rig.registry.state("tuner");
        return tuner.at("note") == "A2" && std::abs(tuner.at("cents").get<double>() - 12.0) < 1.0 &&
               tuner.at("confidence").get<double>() > 0.9;
    }));

    monitors.rig.registry.setSetting("tuner.referencePitch", 442.0);
    CHECK(processUntil(
        [&] { return std::abs(monitors.rig.registry.state("tuner").at("cents").get<double>() - 4.2) < 1.0; }));

    monitors.rig.run("view.toggleTuner");
    CHECK(monitors.rig.registry.state("tuner").at("source").is_null());
}

TEST_CASE("the spectrogram lists the loudest peak of the master") {
    MonitorRig monitors;
    const std::string vox = monitors.rig.run("track.addAudio", {{"name", "Synth"}}).at("id");
    monitors.rig.run("track.setMonitor", {{"track", vox}, {"value", true}});
    monitors.rig.run("view.toggleSpectrogram");
    CHECK(monitors.rig.registry.state("spectrogram").at("source").at("track") == "master");
    monitors.playTone(1000.0);
    CHECK(processUntil([&] {
        const Json peaks = monitors.rig.registry.state("spectrogram").at("peaks");
        return !peaks.empty() && std::abs(peaks.at(0).at("hz").get<double>() - 1000.0) < 2.0;
    }));
    auto& spectrogram = monitors.monitors->spectrogram;
    CHECK(!spectrogram.takeRows().empty());

    monitors.rig.registry.setSetting("spectrogram.fftSize", "16384");
    CHECK(monitors.rig.registry.state("spectrogram").at("binHz").get<double>() < 3.0);
}
