#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

const model::Track& trackOf(const MainWindow& main, const std::string& id) {
    return *main.rig.controller->project().track(model::TrackId{id});
}

QQuickItem* inMixer(const MainWindow& main, const QString& objectName) {
    QQuickItem* found = nullptr;
    processUntil([&] {
        found = MainWindow::find(main.item("mixer"), objectName);
        return found != nullptr;
    });
    REQUIRE(found != nullptr);
    return found;
}

}  // namespace

TEST_CASE("X opens the mixer, whose strips mute, solo, arm and set gain through the track actions") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    processUntil([] { return false; }, 50);

    QTest::keyClick(main.window, Qt::Key_X);
    REQUIRE(processUntil([&] { return main.rig.nav.mixerOpen(); }));

    main.click(inMixer(main, "track.setMute:" + qid(vox)));
    CHECK(trackOf(main, vox).mute);
    main.click(inMixer(main, "track.setSolo:" + qid(vox)));
    CHECK(trackOf(main, vox).solo);
    main.click(inMixer(main, "track.setArm:" + qid(vox)));
    CHECK(trackOf(main, vox).armed);
    main.click(inMixer(main, "track.setArm:" + qid(vox)));

    const double gain = trackOf(main, vox).gainDb;
    QQuickItem* fader = inMixer(main, "track.setGain:" + qid(vox));
    QQuickItem* handle = fader->property("handle").value<QQuickItem*>();
    REQUIRE(handle != nullptr);
    main.drag(handle, {0, -fader->height() / 4});
    CHECK(trackOf(main, vox).gainDb > gain);
    CHECK(main.rig.controller->project().undoName() == "set track gain");

    CHECK(inMixer(main, "mixer.output:" + qid(vox))->property("text").toString().startsWith("Interface"));

    main.click(main.item("view.toggleMixer"));
    CHECK(processUntil([&] { return !main.rig.nav.mixerOpen(); }));
    MainWindow::settle();
}
