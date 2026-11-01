#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/audio_file.hpp"
#include "emerick/engine/audio_analysis.hpp"
#include "project_rig.hpp"

using Catch::Approx;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

std::vector<float> sine(double hz, double amplitude, double seconds) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * kRate));
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        samples[frame] =
            static_cast<float>(amplitude * std::sin(2 * std::numbers::pi * hz * static_cast<double>(frame) / kRate));
    }
    return samples;
}

std::vector<float> noise(double amplitude, double seconds) {
    std::mt19937 random(11);
    std::uniform_real_distribution<double> spread(-amplitude, amplitude);
    std::vector<float> samples(static_cast<std::size_t>(seconds * kRate));
    for (float& sample : samples) {
        sample = static_cast<float>(spread(random));
    }
    return samples;
}

// A Hann shaped 20 ms blip of hz every interval seconds.
std::vector<float> blips(double hz, double interval, double seconds) {
    std::vector<float> samples = sine(hz, 0.5, seconds);
    const auto period = static_cast<std::size_t>(interval * kRate);
    constexpr std::size_t kBlip = 960;
    for (std::size_t frame = 0; frame < samples.size(); ++frame) {
        const std::size_t phase = frame % period;
        samples[frame] *=
            phase < kBlip
                ? static_cast<float>(0.5 * (1 - std::cos(2 * std::numbers::pi * static_cast<double>(phase) / kBlip)))
                : 0.0F;
    }
    return samples;
}

std::string addTrack(ProjectRig& rig, const std::string& name, const std::vector<float>& samples,
                     const std::string& at = "1.1.0") {
    const std::string id = rig.run("track.addAudio", {{"name", name}}).at("id");
    writeWav(rig.path(name + ".wav"), {samples}, kRate);
    rig.run("clip.import", {{"track", id}, {"path", rig.path(name + ".wav").string()}, {"at", at}});
    return id;
}

std::vector<Json> issuesOf(const Json& inspection, const std::string& kind, const std::string& track = {}) {
    std::vector<Json> found;
    for (const Json& issue : inspection.at("issues")) {
        if (issue.at("kind") == kind && (track.empty() || issue.at("track") == track)) {
            found.push_back(issue);
        }
    }
    return found;
}

}  // namespace

TEST_CASE("audio analyze measures a file on the project's bar grid") {
    ProjectRig rig;
    const std::vector<float> tone = sine(1000, 0.1, 4);
    writeWav(rig.path("tone.wav"), {tone, tone}, kRate);

    const Json whole = rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}});
    CHECK(whole.at("integratedLufs").get<double>() == Approx(-20.0).margin(0.1));
    CHECK(whole.at("samplePeakDbfs").get<double>() == Approx(-20.0).margin(0.1));
    CHECK(whole.at("bars").size() == 2);
    CHECK(whole.at("bars")[0].at("rmsDb").get<double>() == Approx(-23.0).margin(0.1));
    CHECK(whole.at("bandsDb").at("mid").get<double>() > whole.at("bandsDb").at("bass").get<double>());
    CHECK(whole.at("silence").empty());
    CHECK(whole.at("clippedSamples") == 0);
    CHECK(whole.at("stereo").at("correlation") == 1.0);

    const Json second = rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}, {"from", 2}, {"to", 4}});
    REQUIRE(second.at("bars").size() == 1);
    CHECK(second.at("bars")[0].at("bar") == 2);
    CHECK_THROWS_AS(rig.run("audio.analyze", {{"path", rig.path("tone.wav").string()}, {"from", 5}}), AppError);
}

TEST_CASE("audio image writes a waveform PNG of the requested size") {
    ProjectRig rig;
    const std::vector<float> tone = sine(1000, 0.5, 4);
    writeWav(rig.path("tone.wav"), {tone, tone}, kRate);

    const Json result = rig.run("audio.image", {{"path", rig.path("tone.wav").string()},
                                                {"out", rig.path("tone.png").string()},
                                                {"width", 400},
                                                {"height", 120}});
    CHECK(result.at("kind") == "waveform");
    CHECK(result.at("seconds") == Approx(4.0));
    const QImage image(QString::fromStdString(rig.path("tone.png").string()));
    CHECK(image.size() == QSize(400, 120));
    CHECK(image.pixelColor(200, 30) != image.pixelColor(200, 5));

    rig.run("audio.image", {{"path", rig.path("tone.wav").string()}});
    CHECK(QImage(QString::fromStdString(rig.path("tone.png").string())).size() == QSize(1200, 300));
    CHECK_THROWS_AS(rig.run("audio.image", {{"path", rig.path("tone.wav").string()}, {"kind", "sonogram"}}), AppError);
}

TEST_CASE("a spectrogram of a 1 kHz sine is brightest near 1 kHz") {
    ProjectRig rig;
    writeWav(rig.path("tone.wav"), {sine(1000, 0.5, 2)}, kRate);
    constexpr int kHeight = 200;
    rig.run("audio.image", {{"path", rig.path("tone.wav").string()},
                            {"out", rig.path("tone.png").string()},
                            {"kind", "spectrogram"},
                            {"width", 100},
                            {"height", kHeight}});

    const QImage image(QString::fromStdString(rig.path("tone.png").string()));
    REQUIRE(image.height() == kHeight);
    int brightestRow = 0;
    int brightest = -1;
    for (int row = 0; row < image.height(); ++row) {
        if (const int gray = qGray(image.pixel(image.width() / 2, row)); gray > brightest) {
            brightest = gray;
            brightestRow = row;
        }
    }
    const double hz = 20 * std::pow(1000.0, static_cast<double>(kHeight - 1 - brightestRow) / (kHeight - 1));
    CHECK(hz == Approx(1000).epsilon(0.05));
}

TEST_CASE("render analyze measures the rendered mix") {
    ProjectRig rig;
    CHECK_THROWS_AS(rig.run("render.analyze"), AppError);

    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(1000, 0.1, 2)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});

    const Json mix = rig.run("render.analyze");
    CHECK(mix.at("channels") == 2);
    CHECK(mix.at("bars").size() == 1);
    CHECK(mix.at("integratedLufs").is_number());
    CHECK(mix.at("stereo").at("correlation") == 1.0);
}

TEST_CASE("a limiter added through the registry holds the mix under its ceiling, and its ceiling automates") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("loud.wav"), {sine(997, std::pow(10.0, -1.0 / 20.0), 2)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("loud.wav").string()}});
    const std::string limiter = rig.run("insert.add", {{"track", vox}, {"type", "limiter"}}).at("id");
    CHECK(rig.run("insert.setParam", {{"insert", limiter}, {"param", "gain"}, {"value", 12}}).at("params").at("gain") ==
          12.0);
    const std::string mix = rig.path("mix.wav").string();
    const auto truePeak = [&] { return emerick::engine::truePeakDbtp(readAudio(mix).channels); };

    rig.run("export.mix", {{"path", mix}});
    CHECK(truePeak() <= -1.0);
    CHECK(truePeak() > -8.0);

    rig.run("automation.addPoint", {{"track", vox}, {"param", limiter + ".ceiling"}, {"tick", 0}, {"value", -6.0}});
    rig.run("export.mix", {{"path", mix}});
    CHECK(truePeak() <= -6.0);
    CHECK(truePeak() > -13.0);
}

TEST_CASE("export mix writes each format by its param or the path's suffix and refuses an unknown one") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(440, 0.5, 2)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});

    for (const auto& [name, format] : kAudioFormats) {
        INFO(name);
        const std::filesystem::path bySuffix = rig.path(std::format("mix.{}", name));
        const std::filesystem::path byParam = rig.path(std::format("{}-mix", name));
        rig.run("export.mix", {{"path", bySuffix.string()}, {"quality", 0.4}});
        rig.run("export.mix", {{"path", byParam.string()}, {"format", name}});
        for (const std::filesystem::path& file : {bySuffix, byParam}) {
            const AudioData audio = readAudio(file);
            CHECK(audio.sampleRate == kRate);
            REQUIRE(audio.channels.size() == 2);
            CHECK(static_cast<double>(audio.channels[0].size()) == Approx(2 * kRate).margin(0.05 * kRate));
            CHECK(emerick::engine::truePeakDbtp(audio.channels) > -12.0);
        }
    }
    CHECK_THROWS_WITH(rig.run("export.mix", {{"path", rig.path("mix.aiff").string()}}),
                      Catch::Matchers::ContainsSubstring("wav, flac, ogg, opus or mp3"));
    CHECK_THROWS_AS(rig.run("export.mix", {{"path", rig.path("mix.ogg").string()}, {"quality", 2}}), AppError);
    CHECK_FALSE(std::filesystem::exists(rig.path("mix.aiff")));

    const Json stems = rig.run("export.stems", {{"folder", rig.path("stems").string()}, {"format", "flac"}});
    REQUIRE(stems.at("files").size() == 1);
    CHECK(stems.at("files")[0].get<std::string>().ends_with("Vox.flac"));
    CHECK(readAudio(stems.at("files")[0].get<std::string>()).channels.size() == 2);
}

TEST_CASE("a song length file encodes to ogg without overflowing the encoder's stack") {
    ProjectRig rig;
    const std::vector<float> song = sine(440, 0.5, 240);
    writeAudio(rig.path("song.ogg"), {song, song}, kRate, {.format = AudioFormat::Ogg});
    const AudioData audio = readAudio(rig.path("song.ogg"));
    REQUIRE(audio.channels.size() == 2);
    CHECK(static_cast<double>(audio.channels[0].size()) == Approx(song.size()).margin(0.01 * kRate));
}

TEST_CASE("opus refuses a rate it cannot encode before it renders") {
    ProjectRig rig({}, 44100);
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("tone.wav"), {std::vector<float>(44100, 0.5F)}, 44100);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});
    CHECK_THROWS_WITH(rig.run("export.mix", {{"path", rig.path("mix.opus").string()}}),
                      Catch::Matchers::ContainsSubstring("Opus only supports sample rates of 8000"));
}

TEST_CASE("the render limit refuses a render longer than it before it starts") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(440, 0.5, 3)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});
    rig.registry.setLimits({.renderSeconds = 2});

    for (const auto& [id, params] :
         std::vector<std::pair<std::string, Json>>{{"export.mix", {{"path", rig.path("mix.wav").string()}}},
                                                   {"export.stems", {{"folder", rig.path("stems").string()}}},
                                                   {"render.analyze", Json::object()},
                                                   {"track.analyze", {{"track", vox}}},
                                                   {"tracks.analyze", Json::object()},
                                                   {"mix.inspect", Json::object()}}) {
        INFO(id);
        CHECK_THROWS_WITH(rig.run(id, params), Catch::Matchers::ContainsSubstring("over the limit of 2 s"));
    }
    CHECK_FALSE(std::filesystem::exists(rig.path("mix.wav")));
    CHECK_FALSE(std::filesystem::exists(rig.path("stems")));

    rig.registry.setLimits({.renderSeconds = 10});
    CHECK(rig.run("export.mix", {{"path", rig.path("mix.wav").string()}}).at("file").is_string());
}

TEST_CASE("export mix with a loudness target lands a quiet mix on it under the ceiling, also from the settings") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeWav(rig.path("quiet.wav"), {sine(440, 0.01, 4)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("quiet.wav").string()}});
    const std::string mix = rig.path("mix.wav").string();

    const Json plain = rig.run("export.mix", {{"path", mix}});
    CHECK_FALSE(plain.contains("integratedLufs"));

    const Json result = rig.run("export.mix", {{"path", mix}, {"targetLufs", -14}, {"ceilingDbtp", -1}});
    CHECK(result.at("integratedLufs").get<double>() == Approx(-14).margin(0.5));
    CHECK(result.at("truePeakDbtp").get<double>() <= -1.0);
    const Json measured = rig.run("audio.analyze", {{"path", mix}});
    CHECK(measured.at("integratedLufs").get<double>() == Approx(-14).margin(0.5));
    CHECK(emerick::engine::truePeakDbtp(readAudio(mix).channels) <= -1.0);

    rig.registry.setSetting("export.normalize", true);
    rig.registry.setSetting("export.targetLufs", -20);
    CHECK(rig.run("export.mix", {{"path", mix}}).at("integratedLufs").get<double>() == Approx(-20).margin(0.5));
    CHECK_FALSE(rig.run("export.mix", {{"path", mix}, {"normalize", false}}).contains("integratedLufs"));
    CHECK_THROWS_AS(rig.run("export.mix", {{"path", mix}, {"ceilingDbtp", 3}}), AppError);
}

TEST_CASE("track analyze measures one track as its stem with its five bands in order, and checks its params") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string bass = rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    const std::string keys = rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(1000, 0.5, 4)}, kRate);
    writeWav(rig.path("low.wav"), {sine(60, 0.5, 4)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});
    rig.run("clip.import", {{"track", bass}, {"path", rig.path("low.wav").string()}});

    CHECK_THROWS_AS(rig.run("track.analyze"), AppError);
    const Json tone = rig.run("track.analyze", {{"track", vox}});
    CHECK(tone.at("track") == vox);
    REQUIRE(tone.at("bands").size() == 5);
    CHECK(tone.at("bands")[1].at("name") == "body");
    CHECK(tone.at("bands")[1].at("lowHz") == 120.0);
    CHECK(tone.at("bands")[1].at("highHz") == 500.0);
    CHECK(tone.at("bands")[2].at("name") == "middle");
    CHECK(tone.at("bands")[2].at("share").get<double>() > 0.95);
    CHECK(tone.at("bands")[4].at("highHz").is_null());
    CHECK(tone.at("centroidHz").get<double>() == Approx(1000).epsilon(0.05));
    CHECK(tone.at("samplePeakDbfs").get<double>() < 0);
    CHECK(tone.at("truePeakDbtp").get<double>() >= tone.at("samplePeakDbfs").get<double>());
    CHECK(tone.at("activeRmsDb").get<double>() == Approx(tone.at("samplePeakDbfs").get<double>() - 3.01).margin(0.2));
    CHECK(tone.at("integratedLufs").is_number());

    CHECK(rig.run("track.analyze", {{"track", bass}}).at("bands")[0].at("share").get<double>() > 0.95);

    const Json second = rig.run("track.analyze", {{"track", vox}, {"from", "2.1.0"}});
    CHECK(second.at("from").at("seconds").get<double>() == Approx(2.0));
    CHECK(second.at("to").at("seconds").get<double>() == Approx(4.0));
    CHECK(second.at("bands")[2].at("share").get<double>() > 0.95);

    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", "nope"}}), AppError);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", keys}}), AppError);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", vox}, {"from", "2.1.0"}, {"to", "1.3.0"}}), AppError);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", vox}, {"from", -5}}), AppError);
}

TEST_CASE("analyzing all tracks lists each audio track and bus with its levels and bands") {
    ProjectRig rig;
    CHECK(rig.run("tracks.analyze").at("tracks").empty());
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    rig.run("track.addMidi", {{"name", "Keys"}});
    const std::string verb = rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    CHECK_THROWS_AS(rig.run("tracks.analyze"), AppError);
    writeWav(rig.path("tone.wav"), {sine(1000, 0.5, 2)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});

    const Json tracks = rig.run("tracks.analyze").at("tracks");
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].at("id") == vox);
    CHECK(tracks[0].at("name") == "Vox");
    CHECK(tracks[0].at("activeRmsDb").get<double>() == Approx(-9.0).margin(0.2));
    CHECK(tracks[0].at("integratedLufs").is_number());
    CHECK(tracks[0].at("truePeakDbtp").is_number());
    CHECK(tracks[0].at("bands").size() == 5);
    CHECK(tracks[1].at("id") == verb);
    CHECK(tracks[1].at("activeRmsDb").is_null());
}

TEST_CASE("analyzing all tracks in one render measures each track and bus as analyzing it alone does") {
    ProjectRig rig;
    const std::string vox = rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    const std::string bass = rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    const std::string verb = rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    const std::string group = rig.run("track.addBus", {{"name", "Group"}}).at("id");
    writeWav(rig.path("tone.wav"), {sine(1000, 0.5, 4)}, kRate);
    writeWav(rig.path("low.wav"), {sine(80, 0.4, 3)}, kRate);
    rig.run("clip.import", {{"track", vox}, {"path", rig.path("tone.wav").string()}});
    rig.run("clip.import", {{"track", bass}, {"path", rig.path("low.wav").string()}, {"at", "2.1.0"}});
    rig.run("insert.add", {{"track", verb}, {"type", "reverb"}});
    rig.run("send.add", {{"track", vox}, {"bus", verb}, {"db", -6.0}});
    rig.run("track.setPan", {{"track", vox}, {"value", -0.5}});
    rig.run("track.setOutputBus", {{"track", bass}, {"bus", group}});
    rig.run("track.setMute", {{"track", bass}, {"value", true}});

    for (const Json& range : {Json::object(), Json{{"from", "2.1.0"}, {"to", "3.3.0"}}}) {
        const Json all = rig.run("tracks.analyze", range).at("tracks");
        REQUIRE(all.size() == 4);
        for (const Json& track : all) {
            Json params = range;
            params["track"] = track.at("id");
            const Json alone = rig.run("track.analyze", params);
            INFO(track.at("name").get<std::string>());
            for (const char* level : {"activeRmsDb", "integratedLufs", "truePeakDbtp"}) {
                CHECK(track.at(level) == alone.at(level));
            }
            CHECK(track.at("bands") == alone.at("bands"));
        }
    }
    CHECK(rig.run("tracks.analyze").at("tracks")[3].at("activeRmsDb").is_number());
}

TEST_CASE("analyzing with a resolution adds a window per bar, beat or step of seconds, with nulls in silence") {
    ProjectRig rig;
    const std::string tone = addTrack(rig, "Tone", sine(3000, 0.5, 4), "2.1.0");
    CHECK_FALSE(rig.run("track.analyze", {{"track", tone}}).contains("windows"));

    const Json bars = rig.run("track.analyze", {{"track", tone}, {"resolution", "bar"}}).at("windows");
    REQUIRE(bars.size() == 3);
    CHECK(bars[0].at("at").at("bbt") == "1.1.0");
    CHECK(bars[0].at("activeRmsDb").is_null());
    CHECK(bars[0].at("tonality").is_null());
    CHECK(bars[0].at("peakDb").is_null());
    CHECK(bars[1].at("at").at("bbt") == "2.1.0");
    CHECK(bars[1].at("at").at("seconds") == Approx(2.0));
    CHECK(bars[1].at("activeRmsDb").get<double>() == Approx(-9.0).margin(0.2));
    CHECK(bars[1].at("tonalPeakHz").get<double>() == Approx(3000).epsilon(0.01));
    CHECK(bars[1].at("tonality").get<double>() > 0.9);
    CHECK(bars[1].at("crestDb").get<double>() == Approx(3.0).margin(0.2));
    CHECK(bars[1].at("bands").size() == 5);
    CHECK(bars[1].at("clipped") == 0);

    CHECK(rig.run("track.analyze", {{"track", tone}, {"resolution", "beat"}, {"from", "2.1.0"}}).at("windows").size() ==
          8);
    CHECK(rig.run("track.analyze", {{"track", tone}, {"resolution", 0.5}}).at("windows").size() == 12);
    const Json all = rig.run("tracks.analyze", {{"resolution", "bar"}}).at("tracks");
    CHECK(all[0].at("windows") == bars);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", tone}, {"resolution", "phrase"}}), AppError);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", tone}, {"resolution", 0.001}}), AppError);
    CHECK_THROWS_AS(rig.run("track.analyze", {{"track", tone}, {"resolution", true}}), AppError);
}

TEST_CASE("inspecting a mix finds a steady whine on its track and bars") {
    ProjectRig rig;
    addTrack(rig, "Room", noise(0.05, 20));
    const std::string whine = addTrack(rig, "Whine", sine(6000, 0.1, 8), "3.1.0");

    const Json inspection = rig.run("mix.inspect");
    INFO(inspection.dump(2));
    const std::vector<Json> whines = issuesOf(inspection, "whine", whine);
    REQUIRE(whines.size() == 1);
    CHECK(whines[0].at("from").at("bbt") == "3.1.0");
    CHECK(whines[0].at("to").at("bbt") == "7.1.0");
    CHECK(whines[0].at("evidence").at("hz").get<double>() == Approx(6000).epsilon(0.01));
    CHECK(whines[0].at("severity") == "high");
}

TEST_CASE("inspecting a mix finds a high pitched blip every eighth note as a beep with its rate and pitch") {
    ProjectRig rig;
    addTrack(rig, "Room", noise(0.05, 16));
    const std::string guitar = addTrack(rig, "Guitar", blips(3000, 0.25, 8), "5.1.0");

    const Json inspection = rig.run("mix.inspect");
    INFO(inspection.dump(2));
    const std::vector<Json> beeps = issuesOf(inspection, "beep", guitar);
    REQUIRE(beeps.size() == 1);
    CHECK(beeps[0].at("from").at("bbt") == "5.1.0");
    CHECK(beeps[0].at("evidence").at("hz").get<double>() == Approx(3000).epsilon(0.02));
    CHECK(beeps[0].at("evidence").at("hitsPerBeat").get<double>() == Approx(2).margin(0.25));
    CHECK(issuesOf(inspection, "whine").empty());
}

TEST_CASE("inspecting a clean mix finds no issues, a too loud track is an imbalance, and a null resolution is a bar") {
    ProjectRig rig;
    addTrack(rig, "Room", noise(0.1, 16));
    addTrack(rig, "Bass", sine(80, 0.3, 16));
    addTrack(rig, "Keys", sine(440, 0.2, 16));
    const Json clean = rig.run("mix.inspect", {{"resolution", "beat"}});
    INFO(clean.dump(2));
    CHECK(clean.at("issues").empty());

    const std::string loud = addTrack(rig, "Lead", sine(300, 0.9, 16));
    const Json inspection = rig.run("mix.inspect", {{"from", "2.1.0"}, {"to", "6.1.0"}});
    INFO(inspection.dump(2));
    const std::vector<Json> imbalances = issuesOf(inspection, "imbalance");
    REQUIRE(imbalances.size() == 1);
    CHECK(imbalances[0].at("track") == loud);
    CHECK(imbalances[0].at("from").at("bbt") == "2.1.0");
    CHECK(imbalances[0].at("to").at("bbt") == "6.1.0");
    CHECK(imbalances[0].at("evidence").at("offsetDb").get<double>() > 8);
    CHECK(rig.run("mix.inspect", {{"from", "2.1.0"}, {"to", "6.1.0"}, {"resolution", nullptr}}) == inspection);
}
