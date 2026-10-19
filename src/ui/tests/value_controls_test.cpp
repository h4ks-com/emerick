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

const model::AudioClip& audioClip(const MainWindow& main, const std::string& id) {
    return std::get<model::AudioClip>(*main.rig.controller->project().findClip(model::ClipId{id})->clip);
}

void typeText(const MainWindow& main, const QString& text) {
    for (const QChar letter : text) {
        QTest::keyClick(main.window, letter.toLatin1());
    }
}

// Opens the number field for typing, types text and presses key.
void typeInto(MainWindow& main, const QString& field, const QString& text, Qt::Key key) {
    MainWindow::settle();
    main.click(main.item(field));
    REQUIRE(processUntil([&] { return main.item(field)->property("editing").toBool(); }));
    typeText(main, text);
    QTest::keyClick(main.window, key);
    MainWindow::settle();
}

}  // namespace

TEST_CASE("dragging a number field runs its action once, at normal and at fine speed") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const QString gain = "track.setGain:" + qid(vox) + ":db";

    main.drag(main.item(gain), {40, 0});
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(5.0));
    CHECK(main.rig.controller->project().undoName() == "set track gain");
    main.rig.run("project.undo");
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(0.0));

    main.drag(main.item(gain), {40, 0}, Qt::ShiftModifier);
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(0.5));
    main.rig.run("project.undo");
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(0.0));
}

TEST_CASE("typing into a number field commits with Enter and cancels with Escape") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const QString gain = "track.setGain:" + qid(vox) + ":db";

    typeInto(main, gain, "-12.5", Qt::Key_Return);
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(-12.5));
    typeInto(main, gain, "+2", Qt::Key_Return);
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(-10.5));
    typeInto(main, gain, "3", Qt::Key_Escape);
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(-10.5));
    CHECK_FALSE(main.item(gain)->property("editing").toBool());
}

TEST_CASE("dragging across mute buttons mutes every track it crosses in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::vector<std::string> tracks{main.rig.run("track.addMidi").at("id"),
                                          main.rig.run("track.addMidi").at("id"),
                                          main.rig.run("track.addMidi").at("id")};
    MainWindow::settle();
    QQuickItem* first = main.item("track.setMute:" + qid(tracks.front()));
    QQuickItem* last = main.item("track.setMute:" + qid(tracks.back()));
    main.drag(first, last->mapToScene({}) - first->mapToScene({}));

    for (std::size_t row = 0; row < tracks.size(); ++row) {
        CAPTURE(row);
        CHECK(trackOf(main, tracks[row]).mute);
    }
    CHECK(main.rig.controller->project().undoName() == "Mute track");
    main.rig.run("project.undo");
    for (const std::string& id : tracks) {
        CHECK_FALSE(trackOf(main, id).mute);
    }
}

TEST_CASE("with several clips selected an Inspector field sets them all, and linked fades move together") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string first =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    const std::string second =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}, {"at", "3.1.0"}})
            .at("id");
    main.rig.run("selection.set", {{"clips", {first, second}}});
    const QString fadeIn = "clip.setFade:" + qid(first) + ":fadeIn";
    CHECK(main.item(fadeIn)->property("displayText").toString().endsWith("2 clips"));

    typeInto(main, fadeIn, "480", Qt::Key_Return);
    CHECK(audioClip(main, first).fadeIn == 480);
    CHECK(audioClip(main, second).fadeIn == 480);
    CHECK(audioClip(main, second).fadeOut == 0);
    main.rig.run("project.undo");
    CHECK(audioClip(main, first).fadeIn == 0);
    CHECK(audioClip(main, second).fadeIn == 0);

    main.rig.run("selection.set", {{"clips", {first}}});
    main.rig.run("edit.toggleLinkFades");
    typeInto(main, "clip.setFade:" + qid(first) + ":fadeOut", "240", Qt::Key_Return);
    CHECK(audioClip(main, first).fadeIn == 240);
    CHECK(audioClip(main, first).fadeOut == 240);
    main.rig.run("edit.toggleLinkFades");
    main.rig.run("selection.clear");
    MainWindow::settle();
}
