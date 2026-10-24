#include <QColor>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

const model::Track& trackOf(const MainWindow& main, const std::string& id) {
    return *main.rig.controller->project().track(model::TrackId{id});
}

// Finds hidden items too, which MainWindow::find skips.
QQuickItem* findHidden(QQuickItem* root, const QString& objectName) {
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findHidden(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("an armed track's take grows on its lane while recording, notes and waveform included, and lands on stop") {
    MainWindow& main = MainWindow::instance();
    const std::string keys = main.rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const std::string vox = main.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    main.rig.run("track.armAll", {{"value", true}});
    main.rig.run("transport.record");
    main.rig.audio->setInput("interface", 1, std::vector<float>(96000, 0.5F));
    main.rig.midi->inject(kMidiPort, std::vector<std::uint8_t>{0x90, 60, 100},
                          main.rig.audio->hostTimeAt(main.rig.audio->processedFrames()));

    QQuickItem* midiTake = main.item("takePreview:" + qid(keys));
    QQuickItem* audioTake = main.item("takePreview:" + qid(vox));
    REQUIRE(processUntil([&] {
        QQuickItem* notes = MainWindow::find(midiTake, "takeNotes");
        return notes != nullptr && notes->property("notes").toList().size() == 1;
    }));
    const double width = midiTake->width();
    REQUIRE(processUntil([&] { return midiTake->width() > width && audioTake->width() > 1.0; }));
    QQuickItem* waveform = MainWindow::find(audioTake, "takeWaveform");
    REQUIRE(waveform != nullptr);
    CHECK(waveform->property("frames").toLongLong() > 0);
    CHECK(trackOf(main, keys).clips.empty());
    CHECK(trackOf(main, vox).clips.empty());

    main.rig.run("transport.stop");
    REQUIRE(processUntil([&] { return !trackOf(main, keys).clips.empty() && !trackOf(main, vox).clips.empty(); }));
    REQUIRE(processUntil([&] { return !midiTake->isVisible() && !audioTake->isVisible(); }));
    main.rig.run("track.armAll", {{"value", false}});
    MainWindow::settle();
}

TEST_CASE("the Inspector's record delay field sets the selected track's delay in milliseconds") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const QString field = "track.setRecordDelay:" + qid(vox);
    MainWindow::settle();

    main.drag(main.item(field), {40, 0});
    CHECK(trackOf(main, vox).recordDelayMs == Catch::Approx(10.0));
    CHECK(main.rig.controller->project().undoName() == "set track record delay");
    main.rig.run("track.setRecordDelay", {{"track", vox}, {"ms", -7.5}});
    MainWindow::settle();
    CHECK(main.item(field)->property("value").toDouble() == Catch::Approx(-7.5));
    CHECK(main.item(field)->property("displayText").toString() == "-7.5 ms");
}

TEST_CASE("a muted clip greys out with a muted icon, and unmuting brings back its color") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    main.rig.run("selection.set", {{"clips", {clip}}});
    MainWindow::settle();
    QQuickItem* icon = findHidden(main.item("clip:" + qid(clip)), "clip.muted:" + qid(clip));
    REQUIRE(icon != nullptr);
    QQuickItem* body = icon->parentItem()->parentItem();
    const QColor color = body->property("color").value<QColor>();
    CHECK_FALSE(icon->isVisible());

    QTest::keyClick(main.window, Qt::Key_M, Qt::MetaModifier);
    REQUIRE(processUntil([&] { return icon->isVisible(); }));
    CHECK(body->property("color").value<QColor>() != color);
    CHECK(main.rig.controller->project().undoName() == "mute clip");

    main.rig.run("project.undo");
    REQUIRE(processUntil([&] { return !icon->isVisible(); }));
    CHECK(body->property("color").value<QColor>() == color);
    main.rig.run("selection.clear");
    MainWindow::settle();
}
