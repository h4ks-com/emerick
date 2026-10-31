#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <string>
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
