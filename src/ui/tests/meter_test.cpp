#include <catch2/catch_test_macros.hpp>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QObject* headerMeter(const MainWindow& main, const std::string& trackId) {
    QQuickItem* reset = main.item("meters.resetClips:" + QString::fromStdString(trackId));
    return reset->parentItem()->property("level").value<QObject*>();
}

double channelLevel(QObject* meter) {
    const auto channels = meter->property("channels").value<QList<QObject*>>();
    REQUIRE(channels.size() == 2);
    return channels.front()->property("level").toDouble();
}

bool clipped(QObject* meter) { return meter->property("clipped").toBool(); }

}  // namespace

TEST_CASE("a track header meter rises during playback, latches its clip light and resets by click or action") {
    MainWindow& main = MainWindow::instance();
    const std::string pad = main.rig.run("track.addAudio").at("id");
    const std::string quiet = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    main.rig.run("clip.import", {{"track", pad}, {"path", main.rig.path("ones.wav").string()}});
    main.rig.run("track.setPan", {{"track", pad}, {"value", -1.0}});

    QObject* padMeter = headerMeter(main, pad);
    QObject* quietMeter = headerMeter(main, quiet);
    CHECK(channelLevel(padMeter) == 0.0);

    main.rig.run("transport.play");
    CHECK(processUntil([&] { return channelLevel(padMeter) == 1.0 && clipped(padMeter); }));
    CHECK(channelLevel(quietMeter) == 0.0);
    CHECK_FALSE(clipped(quietMeter));
    const Json meters = transportState(*main.rig.controller).at("meters");
    CHECK(meters.at(pad).at("output").at("clipped") == true);
    CHECK(meters.at(pad).at("output").at("peakDb") == 0.0);
    main.rig.run("transport.stop");

    main.click(main.item("meters.resetClips:" + QString::fromStdString(pad)));
    CHECK_FALSE(clipped(padMeter));

    main.rig.run("transport.gotoStart");
    main.rig.run("transport.play");
    CHECK(processUntil([&] { return clipped(padMeter); }));
    main.rig.run("transport.stop");
    main.rig.run("meters.resetClips");
    CHECK_FALSE(clipped(padMeter));
    CHECK(processUntil([&] { return channelLevel(padMeter) == 0.0; }, 5000));
    MainWindow::settle();
}
