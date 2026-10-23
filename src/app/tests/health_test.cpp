#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "emerick/model/json.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr core::Frames kFileFrames = 4800;

model::AudioClip clipOf(const std::string& file, core::Ticks length) {
    model::AudioClip clip;
    clip.file = "audio/" + file;
    clip.length = length;
    return clip;
}

std::set<std::pair<std::string, std::string>> problems(const Json& issues) {
    std::set<std::pair<std::string, std::string>> found;
    for (const Json& issue : issues) {
        found.emplace(issue.at("problem"), issue.value("clip", issue.at("track").get<std::string>()));
    }
    return found;
}

}  // namespace

TEST_CASE("checking a project lists missing files, clips past their file, other rates and missing devices") {
    ProjectRig rig;
    const std::filesystem::path bundle = rig.path("Broken.erk");
    const std::vector<float> samples(kFileFrames, 0.5F);
    std::filesystem::create_directories(bundle / "audio");
    writeWav(bundle / "audio" / "fine.wav", {samples}, kRate);
    writeWav(bundle / "audio" / "short.wav", {samples}, kRate);
    writeWav(bundle / "audio" / "slow.wav", {samples}, 44100.0);

    model::Document document;
    const core::Ticks fileLength = document.tempo.framesToTicks(kFileFrames, kRate);
    const model::AudioClip fine = clipOf("fine.wav", fileLength);
    const model::AudioClip gone = clipOf("gone.wav", fileLength);
    const model::AudioClip tooLong = clipOf("short.wav", 2 * fileLength);
    const model::AudioClip slow = clipOf("slow.wav", document.tempo.framesToTicks(kFileFrames, 44100.0));
    model::Track healthy;
    healthy.name = "Healthy";
    healthy.audioOut = {.deviceId = "interface", .channels = {1, 2}};
    healthy.midiIn = {.portId = kMidiPort};
    healthy.clips = {fine};
    model::Track broken;
    broken.name = "Broken";
    broken.audioOut = {.deviceId = "unplugged", .channels = {1, 2}};
    broken.midiIn = {.portId = "Gone"};
    broken.clips = {gone, tooLong, slow};
    document.tracks = {healthy, broken};
    model::saveProject(document, bundle);

    rig.run("project.open", {{"path", bundle.string()}});
    const auto expected = std::set<std::pair<std::string, std::string>>{
        {"missingFile", gone.id.value},
        {"clipPastSource", tooLong.id.value},
        {"sampleRate", slow.id.value},
        {"missingDevice", broken.id.value},
    };
    CHECK(problems(toJson(rig.controller->healthIssues())) == expected);
    CHECK(rig.controller->healthIssues().size() == 5);

    const Json checked = rig.run("project.health");
    CHECK(problems(checked.at("issues")) == expected);
    CHECK(rig.nav.healthOpen());

    rig.run("project.new");
    CHECK(rig.controller->healthIssues().isEmpty());
}
