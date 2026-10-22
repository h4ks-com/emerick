#include <QTest>
#include <catch2/catch_approx.hpp>
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

    MainWindow::settle();
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

    QQuickItem* knob = inMixer(main, "track.setPan:" + qid(vox) + ":knob");
    main.drag(knob, {0, -40});
    CHECK(trackOf(main, vox).pan == Catch::Approx(0.2));
    CHECK(knob->property("toolTip").toString() == "Set track pan   R 20");
    CHECK(main.rig.controller->project().undoName() == "set track pan");

    main.click(main.item("view.toggleMixer"));
    CHECK(processUntil([&] { return !main.rig.nav.mixerOpen(); }));
    MainWindow::settle();
}

TEST_CASE("a mixer strip adds a send from its bus picker, and a send level drag runs live and undoes in one step") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const std::string verb = main.rig.run("track.addBus", {{"name", "Verb"}}).at("id");
    main.rig.nav.setMixerOpen(true);
    MainWindow::settle();

    QQuickItem* add = inMixer(main, "send.add:" + qid(vox));
    main.click(add);
    auto* picker = add->property("menu").value<QObject*>();
    REQUIRE(picker != nullptr);
    REQUIRE(processUntil([&] { return picker->property("opened").toBool(); }));
    QMetaObject::invokeMethod(menuItem(picker, "Verb")->property("action").value<QObject*>(), "trigger");
    REQUIRE(processUntil([&] { return !picker->property("visible").toBool(); }));
    REQUIRE(trackOf(main, vox).sends.size() == 1);
    CHECK(trackOf(main, vox).sends.front().bus == model::TrackId{verb});
    const auto undoBefore = main.rig.controller->project().undoName();
    MainWindow::settle();

    QQuickItem* level = inMixer(main, "send.setLevel:" + qid(vox) + ":" + qid(verb));
    const QPointF press = level->mapToScene({level->width() / 2, level->height() / 2});
    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        main.send(QEvent::MouseMove, press - QPointF{8.0 * step, 0}, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return trackOf(main, vox).sends.front().levelDb < 0.0; }));
    main.send(QEvent::MouseButtonRelease, press - QPointF{40, 0}, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK(main.rig.controller->project().undoName() == "set send level");
    main.rig.run("project.undo");
    CHECK(trackOf(main, vox).sends.front().levelDb == 0.0);
    CHECK(main.rig.controller->project().undoName() == undoBefore);

    main.rig.nav.setMixerOpen(false);
    MainWindow::settle();
}

TEST_CASE("a track header pan knob pans live while dragged, undoes in one step, and a double click centers it") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    MainWindow::settle();
    QQuickItem* knob = main.item("track.setPan:" + qid(vox) + ":knob");
    const QPointF press = knob->mapToScene({knob->width() / 2, knob->height() / 2});
    const auto undoBefore = main.rig.controller->project().undoName();

    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        main.send(QEvent::MouseMove, press - QPointF{8.0 * step, 0}, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return trackOf(main, vox).pan == Catch::Approx(-0.2); }));
    main.send(QEvent::MouseButtonRelease, press - QPointF{40, 0}, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK(knob->property("toolTip").toString() == "Set track pan   L 20");
    CHECK(main.rig.controller->project().undoName() == "set track pan");
    main.rig.run("project.undo");
    CHECK(trackOf(main, vox).pan == 0.0);
    CHECK(main.rig.controller->project().undoName() == undoBefore);

    main.rig.run("track.setPan", {{"track", vox}, {"value", 0.5}});
    MainWindow::settle();
    QTest::mouseDClick(main.window, Qt::LeftButton, Qt::NoModifier, press.toPoint());
    CHECK(processUntil([&] { return trackOf(main, vox).pan == 0.0; }));
    main.rig.run("track.setPan", {{"track", vox}, {"value", -0.5}});
    MainWindow::settle();
    QTest::mouseClick(main.window, Qt::LeftButton, Qt::AltModifier, press.toPoint());
    CHECK(processUntil([&] { return trackOf(main, vox).pan == 0.0; }));
}

TEST_CASE("dragging down across track pan knobs and then sideways pans every gathered track in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::string first = main.rig.run("track.addAudio").at("id");
    const std::string second = main.rig.run("track.addAudio").at("id");
    MainWindow::settle();
    QQuickItem* from = main.item("track.setPan:" + qid(first) + ":knob");
    const QPointF down = main.item("track.setPan:" + qid(second) + ":knob")->mapToScene({}) - from->mapToScene({});

    main.drag(from, {down, down + QPointF{40, 0}});
    CHECK(trackOf(main, first).pan == Catch::Approx(0.2));
    CHECK(trackOf(main, second).pan == Catch::Approx(0.2));
    main.rig.run("project.undo");
    CHECK(trackOf(main, first).pan == 0.0);
    CHECK(trackOf(main, second).pan == 0.0);
}
