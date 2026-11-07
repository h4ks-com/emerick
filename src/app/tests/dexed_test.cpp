#include "emerick/app/dexed.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

std::vector<std::uint8_t> cartridgeSysex(const std::vector<std::string>& names) {
    std::vector<std::uint8_t> sysex{0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    for (std::size_t program = 0; program < kDx7Programs; ++program) {
        std::vector<std::uint8_t> voice(128, static_cast<std::uint8_t>(program));
        const std::string name = program < names.size() ? names[program] : "VOICE " + std::to_string(program + 1);
        std::ranges::fill(voice.begin() + 118, voice.end(), ' ');
        std::ranges::copy(name, voice.begin() + 118);
        sysex.insert(sysex.end(), voice.begin(), voice.end());
    }
    sysex.push_back(0);
    sysex.push_back(0xF7);
    return sysex;
}

// A Dexed state as JUCE writes it, with a cartridge of its own and the bytes of a voice in its base64.
std::vector<std::uint8_t> dexedStateBytes() {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?> <dexedState cutoff="1.0" currentProgram="0" opSwitch="101010">)"
        R"(<dexedBlob base64:sysex="0." base64:program="0."/></dexedState>)";
    std::vector<std::uint8_t> bytes{0x56, 0x43, 0x32, 0x21, static_cast<std::uint8_t>(xml.size()), 0, 0, 0};
    bytes.insert(bytes.end(), xml.begin(), xml.end());
    bytes.push_back(0);
    return bytes;
}

struct DexedRig : ProjectRig {
    DexedRig() {
        registerPluginActions(registry, nav, catalog);
        registerInstrumentActions(registry, *controller, nav, catalog, path("libraries"));
        registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    }

    PluginCatalog catalog{{.cacheFolder = path("cache"), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}}};
};

}  // namespace

TEST_CASE("a DX7 cartridge names its 32 programs, found after other sysex in the file") {
    std::vector<std::uint8_t> file{0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    const auto sysex = cartridgeSysex({"E.PIANO 1", "BRASS~1"});
    file.insert(file.end(), sysex.begin(), sysex.end());

    const auto cartridge = parseDx7Cartridge(file);
    REQUIRE(cartridge.has_value());
    CHECK(cartridge->sysex == sysex);
    const auto names = dx7ProgramNames(*cartridge);
    REQUIRE(names.size() == kDx7Programs);
    CHECK(names[0] == "E.PIANO 1");
    CHECK(names[1] == "BRASS>1");
    CHECK(names[31] == "VOICE 32");
    CHECK_FALSE(parseDx7Cartridge(std::vector<std::uint8_t>(sysex.begin(), sysex.end() - 1)).has_value());
}

TEST_CASE("a Dexed state takes a cartridge and a program, which it keeps through its own encoding") {
    const std::vector<std::uint8_t> empty = dexedStateBytes();
    CHECK_FALSE(parseDexedState(empty).has_value());

    const Dx7Cartridge cartridge{cartridgeSysex({"E.PIANO 1", "BRASS 1", "STRINGS 1"})};
    const DexedState blank{.xml = {empty.begin() + 8, empty.end() - 1},
                           .cartridge = cartridge,
                           .voice = std::vector<std::uint8_t>(161, 7),
                           .program = 0};

    const auto picked = parseDexedState(dexedStateWith(blank, cartridge, 2));
    REQUIRE(picked.has_value());
    CHECK(picked->program == 2);
    CHECK(picked->cartridge.sysex == cartridge.sysex);
    REQUIRE(picked->voice.size() == 161);
    CHECK(std::string(picked->voice.begin() + 145, picked->voice.begin() + 155) == "STRINGS 1 ");
    CHECK(picked->voice[0] == 2);
    CHECK(picked->voice[160] == 7);
    CHECK(picked->xml.contains(R"(opSwitch="111111")"));
    CHECK(picked->xml.contains(R"(cutoff="1.0")"));

    const auto withProgram = [&](std::string_view program) {
        std::vector<std::uint8_t> bytes = dexedStateWith(blank, cartridge, 2);
        const auto at =
            static_cast<std::ptrdiff_t>(std::string(bytes.begin(), bytes.end()).find(R"(currentProgram="2")") + 16);
        bytes.erase(bytes.begin() + at);
        bytes.insert(bytes.begin() + at, program.begin(), program.end());
        const std::size_t length = bytes.size() - 9;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            bytes.at(4 + byte) = static_cast<std::uint8_t>(length >> (8 * byte));
        }
        return parseDexedState(bytes);
    };
    CHECK(withProgram("17").value().program == 17);
    CHECK_FALSE(withProgram("40").has_value());
}

TEST_CASE("dexed.load picks a DX7 program by name, which the saved project and an export play") {
    DexedRig rig;
    if (!std::filesystem::exists(bundledClapFolder(EMERICK_GM_APP_FOLDER) / "Dexed.clap")) {
        SKIP("Dexed does not ship on this platform");
    }
    const std::string track = rig.run("track.addMidi").at("id");
    const Json listed = rig.run("dexed.programs", {{"cartridge", "synprezfm_01"}});
    CHECK(listed.at("programs")[1].at("name") == "HARPSICH 1");
    CHECK(std::ranges::contains(listed.at("cartridges"), Json("dexed_01")));

    const Json loaded =
        rig.run("dexed.load", {{"track", track}, {"cartridge", "synprezfm_01"}, {"program", "harpsich 1"}});
    CHECK(loaded.at("program") == 2);
    CHECK(loaded.at("name") == "HARPSICH 1");
    const model::TrackId id{track};
    CHECK(rig.controller->project().track(id)->instrument->pluginId == kDexedPluginId);
    CHECK(rig.run("dexed.programs", {{"track", track}}).at("program") == 2);
    CHECK(rig.run("dexed.load", {{"track", track}, {"program", 15}}).at("name") == "OB8 BRASS");
    rig.run("project.undo");
    CHECK(rig.run("dexed.programs", {{"track", track}}).at("program") == 2);
    rig.run("project.redo");
    CHECK_THROWS_AS(rig.run("dexed.load", {{"track", track}, {"program", 33}}), AppError);
    CHECK_THROWS_AS(rig.run("dexed.load", {{"track", track}, {"program", "no such sound"}}), AppError);

    rig.controller->project().editTrack(id, "add note", [](model::Track& edited) {
        model::MidiClip clip;
        clip.length = 1920;
        clip.notes = {{.pitch = 60, .velocity = 100, .start = 0, .length = 960, .channel = 1}};
        edited.clips.emplace_back(clip);
    });
    const std::string mix = rig.path("mix.wav").string();
    rig.run("export.mix", {{"path", mix}});
    const auto samples = readAudio(mix).channels.at(0);
    CHECK(std::ranges::max(samples | std::views::transform([](float sample) { return std::abs(sample); })) > 0.01F);

    rig.controller->flush();
    rig.controller->save();
    DexedRig other;
    other.controller->open(rig.controller->folder());
    const auto saved = parseDexedState(other.controller->project().track(id)->instrument->state);
    REQUIRE(saved.has_value());
    CHECK(saved->program == 14);
    CHECK(std::string(saved->voice.begin() + 145, saved->voice.begin() + 155) == "OB8 BRASS ");
    CHECK(dx7ProgramNames(saved->cartridge)[1] == "HARPSICH 1");
}
