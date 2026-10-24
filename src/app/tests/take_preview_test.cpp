#include <sndfile.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

// The frame where the file first holds a sound on any channel.
core::Frames firstSound(const std::filesystem::path& path) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    REQUIRE(file != nullptr);
    std::vector<float> samples(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_float(file, samples.data(), info.frames);
    sf_close(file);
    const auto sound = std::ranges::find_if(samples, [](float sample) { return sample != 0.0F; });
    return (sound - samples.begin()) / info.channels;
}

std::size_t previewNotes(const ProjectController& controller, const model::TrackId& id) {
    const TakePreview* preview = controller.takePreview(id);
    if (preview == nullptr || !preview->track || preview->track->clips.empty()) {
        return 0;
    }
    return std::get<model::MidiClip>(preview->track->clips.back()).notes.size();
}

}  // namespace

TEST_CASE("a MIDI take shows its notes before stop, and only the landed take adds an undo step") {
    ProjectRig rig;
    const model::TrackId id{rig.run("track.addMidi", {{"name", "Keys"}}).at("id").get<std::string>()};
    rig.run("track.setArm", {{"track", id.value}, {"value", true}});
    const auto undoBefore = rig.controller->project().undoName();
    rig.run("transport.record");
    rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x90, 60, 100},
                     rig.audio->hostTimeAt(rig.audio->processedFrames()));

    REQUIRE(processUntil([&] { return previewNotes(*rig.controller, id) == 1; }));
    const core::Ticks length = rig.controller->takePreview(id)->length;
    REQUIRE(processUntil([&] { return rig.controller->takePreview(id)->length > length; }));
    CHECK(rig.controller->project().track(id)->clips.empty());
    CHECK(rig.controller->project().undoName() == undoBefore);

    rig.run("transport.stop");
    REQUIRE(processUntil([&] { return !rig.controller->project().track(id)->clips.empty(); }));
    CHECK(rig.controller->takePreview(id) == nullptr);
    CHECK(rig.controller->project().undoName() == "record take on Keys");
}

TEST_CASE("an audio take's preview grows and sends its peaks before the take lands") {
    ProjectRig rig;
    const model::TrackId id{rig.run("track.addAudio", {{"name", "Vox"}}).at("id").get<std::string>()};
    rig.run("track.setArm", {{"track", id.value}, {"value", true}});
    std::size_t peaks = 0;
    std::filesystem::path peakFile;
    QObject::connect(rig.controller.get(), &ProjectController::takePeaks,
                     [&](const std::filesystem::path& file, const Peaks& added) {
                         peakFile = file;
                         peaks += added.maximum.size();
                     });
    rig.run("transport.record");
    rig.audio->setInput("interface", 1, std::vector<float>(96000, 0.25F));

    REQUIRE(processUntil([&] { return peaks > 4; }));
    const TakePreview* preview = rig.controller->takePreview(id);
    REQUIRE(preview != nullptr);
    CHECK(preview->audioFile == peakFile);
    const core::Frames frames = preview->frames;
    const core::Ticks length = preview->length;
    REQUIRE(processUntil([&] { return rig.controller->takePreview(id)->frames > frames; }));
    CHECK(rig.controller->takePreview(id)->length > length);
    CHECK(rig.controller->project().track(id)->clips.empty());

    rig.run("transport.stop");
    REQUIRE(processUntil([&] { return !rig.controller->project().track(id)->clips.empty(); }));
    CHECK(rig.controller->takePreview(id) == nullptr);
}

TEST_CASE("a track's record delay moves its next take later by the delay's frames, and undoes") {
    ProjectRig rig;
    const std::string id = rig.run("track.addAudio", {{"name", "Genos"}}).at("id");
    rig.run("track.setArm", {{"track", id}, {"value", true}});
    CHECK(rig.run("track.setRecordDelay", {{"track", id}, {"ms", 10}}).at("recordDelayMs") == 10.0);
    CHECK_THROWS_AS(rig.run("track.setRecordDelay", {{"track", id}, {"ms", 5000}}), AppError);

    rig.controller->flush();
    REQUIRE(rig.controller->engine().streamOpen());
    rig.audio->setInput("interface", 1, std::vector<float>(48000, 0.25F));
    rig.run("transport.record");
    rig.audio->process(24000);
    rig.run("transport.stop");
    REQUIRE(rig.waitForTakes(1) == 1);
    const Json clip = projectState(*rig.controller).at("tracks").at(0).at("clips").at(0);
    CHECK(firstSound(rig.controller->folder() / clip.at("file").get<std::string>()) == 480);

    rig.run("project.undo");
    rig.run("project.undo");
    CHECK(rig.controller->project().track(model::TrackId{id})->recordDelayMs == 0.0);
}
