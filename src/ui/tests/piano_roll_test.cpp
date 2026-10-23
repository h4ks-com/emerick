#include <QTest>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <variant>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

struct Roll {
    MainWindow& main = MainWindow::instance();
    std::string track = main.rig.run("track.addMidi").at("id");
    std::string clip = main.rig.run("midi.addClip", {{"track", track}, {"at", 0}}).at("id");

    Roll() {
        main.rig.nav.setScrollTicks(0);
        main.rig.run("midi.editClip", {{"clip", clip}});
        REQUIRE(main.item("pianoRoll.grid")->isVisible());
        MainWindow::settle();
    }

    [[nodiscard]] const model::MidiClip& midi() const {
        return std::get<model::MidiClip>(*main.rig.controller->project().findClip(model::ClipId{clip})->clip);
    }

    [[nodiscard]] const model::Note& note(const std::string& id) const {
        return *std::ranges::find(midi().notes, model::NoteId{id}, &model::Note::id);
    }

    std::string add(int pitch, core::Ticks start) const {
        return main.rig.run("midi.addNote", {{"clip", clip}, {"pitch", pitch}, {"start", start}, {"length", 480}})
            .at("notes")
            .at(0)
            .at("id");
    }

    [[nodiscard]] double call(const char* function, double value) const {
        QVariant result;
        QMetaObject::invokeMethod(main.item("pianoRoll"), function, Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, value));
        return result.toDouble();
    }

    // The scene point inside the grid cell of pitch at ticks from the clip start.
    [[nodiscard]] QPointF cell(core::Ticks ticks, int pitch) const {
        const double keyHeight = main.rig.nav.keyHeight();
        return main.item("pianoRoll.grid")
            ->mapToScene({call("xOf", static_cast<double>(ticks)) + 2, call("yOf", pitch) + keyHeight / 2});
    }

    [[nodiscard]] double keyHeight() const { return main.rig.nav.keyHeight(); }
    [[nodiscard]] double pixelsPerTick() const { return main.rig.nav.pixelsPerTick(); }
};

}  // namespace

TEST_CASE("double-clicking a MIDI clip opens it in the piano roll") {
    MainWindow& main = MainWindow::instance();
    main.rig.nav.setPianoRollOpen(false);
    main.rig.nav.setScrollTicks(0);
    const std::string track = main.rig.run("track.addMidi").at("id");
    const std::string clip = main.rig.run("midi.addClip", {{"track", track}, {"at", 0}}).at("id");
    MainWindow::settle();
    QQuickItem* body = main.item("clip.move:" + qid(clip));
    QTest::mouseDClick(main.window, Qt::LeftButton, Qt::NoModifier,
                       body->mapToScene({body->width() / 2, body->height() / 2}).toPoint());
    CHECK(processUntil([&] { return main.rig.nav.pianoRollOpen(); }));
    CHECK(main.rig.nav.pianoRollClip() == qid(clip));
    CHECK(main.item("pianoRoll.grid")->isVisible());
    MainWindow::settle();
}

TEST_CASE("a Command-click on the grid adds a note on the grid where it was clicked") {
    Roll roll;
    const QPointF at = roll.cell(500, 62);
    roll.main.send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::ControlModifier);
    roll.main.send(QEvent::MouseButtonRelease, at, Qt::NoButton, Qt::ControlModifier);
    REQUIRE(roll.midi().notes.size() == 1);
    CHECK(roll.midi().notes[0].pitch == 62);
    CHECK(roll.midi().notes[0].start == 480);
    CHECK(roll.main.rig.controller->project().undoName() == "add note");
    CHECK(roll.main.rig.nav.selectedNotes() == QStringList{qid(roll.midi().notes[0].id.value)});
    MainWindow::settle();
}

TEST_CASE("dragging a note moves it live in time and pitch as one undo step") {
    Roll roll;
    const std::string id = roll.add(60, 0);
    const QPointF from = roll.cell(240, 60);
    const QPointF offset{960 * roll.pixelsPerTick(), -2 * roll.keyHeight()};
    roll.main.send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        roll.main.send(QEvent::MouseMove, from + offset * step / 5, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return roll.note(id).start == 960; }));
    CHECK(roll.note(id).pitch == 62);
    roll.main.send(QEvent::MouseButtonRelease, from + offset, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();

    CHECK(roll.main.rig.controller->project().undoName() == "move notes");
    roll.main.rig.run("project.undo");
    CHECK((roll.note(id).start == 0 && roll.note(id).pitch == 60));
    CHECK(roll.main.rig.controller->project().undoName() == "add note");
}

TEST_CASE("dragging a note's right edge resizes it on the grid") {
    Roll roll;
    const std::string id = roll.add(64, 0);
    roll.main.drag(roll.main.item("midi.resizeNotes:" + qid(id)), {480 * roll.pixelsPerTick(), 0});
    CHECK(roll.note(id).length == 960);
    CHECK(roll.note(id).start == 0);
    CHECK(roll.main.rig.controller->project().undoName() == "resize notes");
}

TEST_CASE("dragging a velocity bar up raises the note's velocity") {
    Roll roll;
    const std::string id = roll.add(60, 960);
    const int before = roll.note(id).velocity;
    roll.main.drag(roll.main.item("midi.setVelocity:" + qid(id)), {0, -20});
    CHECK(roll.note(id).velocity > before);
    CHECK(roll.main.rig.controller->project().undoName() == "set note velocity");
}

TEST_CASE("Delete in the focused piano roll removes the selected notes and keeps the clip") {
    Roll roll;
    const std::string kept = roll.add(60, 0);
    const std::string gone = roll.add(62, 960);
    roll.main.click(roll.main.item("note:" + qid(gone)));
    CHECK(roll.main.rig.nav.selectedNotes() == QStringList{qid(gone)});
    QTest::keyClick(roll.main.window, Qt::Key_Delete);
    REQUIRE(roll.midi().notes.size() == 1);
    CHECK(roll.midi().notes[0].id.value == kept);

    roll.main.rig.run("midi.selectNotes", {{"notes", {kept}}});
    QTest::keyClick(roll.main.window, Qt::Key_Up);
    CHECK(roll.note(kept).pitch == 61);
    QTest::keyClick(roll.main.window, Qt::Key_Right);
    CHECK(roll.note(kept).start == 240);
    MainWindow::settle();
}

TEST_CASE("the piano roll fits and centers its keys on the notes of the clip it opens, and an empty clip on middle C") {
    Roll empty;
    QQuickItem* grid = empty.main.item("pianoRoll.grid");
    const double middleC = empty.call("yOf", 60) + empty.keyHeight() / 2;
    CHECK(std::abs(middleC - grid->height() / 2) <= empty.keyHeight());

    const std::string track = empty.main.rig.run("track.addMidi").at("id");
    const std::string clip = empty.main.rig.run("midi.addClip", {{"track", track}, {"at", 0}}).at("id");
    for (const int pitch : {60, 72, 84}) {
        empty.main.rig.run("midi.addNote", {{"clip", clip}, {"pitch", pitch}, {"start", 0}});
    }
    REQUIRE(25 * empty.keyHeight() > grid->height());
    empty.main.rig.run("midi.editClip", {{"clip", clip}});
    MainWindow::settle();
    CHECK(empty.call("yOf", 84) >= 0);
    CHECK(empty.call("yOf", 60) + empty.keyHeight() <= grid->height());
    const double middle = (empty.call("yOf", 60) + empty.call("yOf", 84) + empty.keyHeight()) / 2;
    CHECK(std::abs(middle - grid->height() / 2) <= empty.keyHeight());
    empty.main.rig.nav.setKeyHeight(kDefaultKeyHeight);
}

TEST_CASE("the piano keys are shaped like a keyboard and a press plays the key under the pointer") {
    const Roll roll;
    QQuickItem* keyboard = roll.main.item("midi.audition")->parentItem();
    const double row = roll.keyHeight();
    const double middle = roll.call("yOf", 61) + row / 2;
    const auto keyAt = [&](double x) {
        QVariant result;
        QMetaObject::invokeMethod(keyboard, "keyAt", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, x),
                                  Q_ARG(QVariant, middle));
        return result.toInt();
    };
    CHECK(keyAt(2) == 61);
    CHECK(keyAt(keyboard->width() - 2) == 60);
    CHECK(roll.main.item("midi.audition:62")->height() == Catch::Approx(row * 5 / 3));
    CHECK(roll.main.item("midi.audition:65")->height() == Catch::Approx(row * 7 / 4));
    CHECK(roll.main.item("midi.audition:61")->width() < roll.main.item("midi.audition:60")->width());
}
