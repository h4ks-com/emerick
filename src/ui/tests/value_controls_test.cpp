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

TEST_CASE("with several clips selected an Inspector field sets them all") {
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
    main.rig.run("selection.clear");
    MainWindow::settle();
}

TEST_CASE("an Inspector fade shape button shapes every selected clip in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string first =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    const std::string second =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}, {"at", "3.1.0"}})
            .at("id");
    main.rig.run("selection.set", {{"clips", {first, second}}});
    MainWindow::settle();
    QQuickItem* button = main.item("clip.setFadeOutShape:equalPower");

    main.click(button);
    MainWindow::settle();
    CHECK(audioClip(main, first).fadeOutShape == core::FadeShape::equalPower);
    CHECK(audioClip(main, second).fadeOutShape == core::FadeShape::equalPower);
    CHECK(audioClip(main, first).fadeInShape == core::FadeShape::linear);
    CHECK(button->property("active").toBool());
    CHECK(main.rig.controller->project().undoName() == "set fade shape");
    main.rig.run("project.undo");
    CHECK(audioClip(main, first).fadeOutShape == core::FadeShape::linear);
    CHECK(audioClip(main, second).fadeOutShape == core::FadeShape::linear);
    main.rig.run("selection.clear");
    MainWindow::settle();
}

TEST_CASE("dragging down across Inspector fields gathers them, and a sideways drag moves them all in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    main.rig.run("selection.set", {{"clips", {clip}}});
    MainWindow::settle();
    QQuickItem* fadeIn = main.item("clip.setFade:" + qid(clip) + ":fadeIn");
    QQuickItem* fadeOut = main.item("clip.setFade:" + qid(clip) + ":fadeOut");
    QQuickItem* gain = main.item("clip.setGain:" + qid(clip) + ":db");
    const QPointF center{fadeIn->width() / 2, fadeIn->height() / 2};
    const QPointF toFadeOut = fadeOut->mapToScene(center) - fadeIn->mapToScene(center);
    const QPointF toGain = gain->mapToScene(center) - fadeIn->mapToScene(center);
    constexpr double kSteps = 10;
    const QPointF sideways{kSteps * 4, 0};
    const double fadeStep = fadeIn->property("step").toDouble();
    const double gainStep = gain->property("step").toDouble();
    const double curveStep = main.item("clip.setFade:" + qid(clip) + ":fadeInCurve")->property("step").toDouble();

    const QPointF press = fadeIn->mapToScene(center);
    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        main.send(QEvent::MouseMove, press + toFadeOut * step / 5, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(fadeIn->property("gathered").toBool());
    CHECK(main.item("clip.setFade:" + qid(clip) + ":fadeInCurve")->property("gathered").toBool());
    CHECK(fadeOut->property("gathered").toBool());
    CHECK_FALSE(gain->property("gathered").toBool());
    main.send(QEvent::MouseButtonRelease, press + toFadeOut, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    CHECK_FALSE(fadeIn->property("gathered").toBool());
    CHECK(audioClip(main, clip).fadeIn == 0);

    main.drag(fadeIn, {toFadeOut, toFadeOut + sideways});
    CHECK(audioClip(main, clip).fadeIn == Catch::Approx(kSteps * fadeStep));
    CHECK(audioClip(main, clip).fadeInCurve == Catch::Approx(kSteps * curveStep));
    CHECK(audioClip(main, clip).fadeOut == Catch::Approx(kSteps * fadeStep));
    CHECK(main.rig.controller->project().undoName() == "set clip fades");
    main.rig.run("project.undo");
    CHECK(audioClip(main, clip).fadeIn == 0);
    CHECK(audioClip(main, clip).fadeOut == 0);

    main.drag(main.item("clip.setFade:" + qid(clip) + ":fadeIn"), {toGain, toGain + sideways});
    CHECK(audioClip(main, clip).fadeIn == Catch::Approx(kSteps * fadeStep));
    CHECK(audioClip(main, clip).fadeOut == Catch::Approx(kSteps * fadeStep));
    CHECK(audioClip(main, clip).gainDb == Catch::Approx(kSteps * gainStep));
    CHECK(audioClip(main, clip).fadeOutCurve == Catch::Approx(kSteps * curveStep));
    main.rig.run("project.undo");
    CHECK(audioClip(main, clip).fadeIn == 0);
    CHECK(audioClip(main, clip).fadeOut == 0);
    CHECK(audioClip(main, clip).fadeOutCurve == 0.0);
    CHECK(audioClip(main, clip).gainDb == Catch::Approx(0.0));
    main.rig.run("selection.clear");
    MainWindow::settle();
}

TEST_CASE("dragging down across track gains and then sideways sets every gathered track in one undo step") {
    MainWindow& main = MainWindow::instance();
    const std::string first = main.rig.run("track.addAudio").at("id");
    const std::string second = main.rig.run("track.addAudio").at("id");
    MainWindow::settle();
    QQuickItem* from = main.item("track.setGain:" + qid(first) + ":db");
    const QPointF down = main.item("track.setGain:" + qid(second) + ":db")->mapToScene({}) - from->mapToScene({});

    main.drag(from, {down, down + QPointF{40, 0}});
    CHECK(trackOf(main, first).gainDb == Catch::Approx(5.0));
    CHECK(trackOf(main, second).gainDb == Catch::Approx(5.0));
    main.rig.run("project.undo");
    CHECK(trackOf(main, first).gainDb == Catch::Approx(0.0));
    CHECK(trackOf(main, second).gainDb == Catch::Approx(0.0));
}

TEST_CASE("the Inspector and the clip follow a fade handle drag while it lasts, and one undo restores the fade") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    main.rig.run("selection.set", {{"clips", {clip}}});
    MainWindow::settle();
    const auto undoBefore = main.rig.controller->project().undoName();
    QQuickItem* handle = main.item("clip.setFade:" + qid(clip) + ":in");
    QQuickItem* field = main.item("clip.setFade:" + qid(clip) + ":fadeIn");
    QQuickItem* clipItem = main.item("clip:" + qid(clip));
    const double pixelsPerTick = main.rig.nav.pixelsPerTick();
    const QPointF press = handle->mapToScene({handle->width() / 2, handle->height() / 2});
    const Qt::KeyboardModifiers offGrid = Qt::ControlModifier;

    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, offGrid);
    for (const double ticks : {100.0, 200.0, 300.0, 400.0}) {
        main.send(QEvent::MouseMove, press + QPointF{ticks * pixelsPerTick, 0}, Qt::LeftButton, offGrid);
    }
    CHECK(processUntil([&] { return field->property("value").toDouble() == 400; }));
    CHECK(clipItem->property("fadeIn").toDouble() == 400);
    CHECK(audioClip(main, clip).fadeIn == 400);
    main.send(QEvent::MouseButtonRelease, press + QPointF{400 * pixelsPerTick, 0}, Qt::NoButton, offGrid);
    MainWindow::settle();
    CHECK(main.rig.controller->project().undoName() == "set clip fades");

    main.rig.run("project.undo");
    CHECK(audioClip(main, clip).fadeIn == 0);
    CHECK(main.rig.controller->project().undoName() == undoBefore);
    CHECK(processUntil([&] { return field->property("value").toDouble() == 0; }));
    CHECK(clipItem->property("fadeIn").toDouble() == 0);

    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, offGrid);
    main.send(QEvent::MouseMove, press + QPointF{300 * pixelsPerTick, 0}, Qt::LeftButton, offGrid);
    CHECK(processUntil([&] { return audioClip(main, clip).fadeIn == 300; }));
    QTest::keyClick(main.window, Qt::Key_Escape);
    CHECK(audioClip(main, clip).fadeIn == 0);
    main.send(QEvent::MouseMove, press + QPointF{350 * pixelsPerTick, 0}, Qt::LeftButton, offGrid);
    main.send(QEvent::MouseButtonRelease, press + QPointF{350 * pixelsPerTick, 0}, Qt::NoButton, offGrid);
    MainWindow::settle();
    CHECK(audioClip(main, clip).fadeIn == 0);
    CHECK(main.rig.controller->project().undoName() == undoBefore);
    main.rig.run("selection.clear");
    MainWindow::settle();
}

TEST_CASE("dragging a track gain field moves its fader while the drag lasts") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    MainWindow::settle();
    QQuickItem* field = main.item("track.setGain:" + qid(vox) + ":db");
    QQuickItem* fader = main.item("track.setGain:" + qid(vox));
    const QPointF press = field->mapToScene({field->width() / 2, field->height() / 2});

    main.send(QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::NoModifier);
    for (int step = 1; step <= 5; ++step) {
        main.send(QEvent::MouseMove, press + QPointF{8.0 * step, 0}, Qt::LeftButton, Qt::NoModifier);
    }
    CHECK(processUntil([&] { return fader->property("value").toDouble() == Catch::Approx(5.0); }));
    main.send(QEvent::MouseButtonRelease, press + QPointF{40, 0}, Qt::NoButton, Qt::NoModifier);
    MainWindow::settle();
    main.rig.run("project.undo");
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(0.0));
}

TEST_CASE("the step arrows step the value without opening the editor, and stay while typing") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const QString gain = "track.setGain:" + qid(vox) + ":db";
    MainWindow::settle();

    main.hover(main.item(gain));
    main.click(main.item(gain + ":up"));
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(0.5));
    CHECK_FALSE(main.item(gain)->property("editing").toBool());

    main.click(main.item(gain));
    REQUIRE(processUntil([&] { return main.item(gain)->property("editing").toBool(); }));
    main.click(main.item(gain + ":up"));
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(1.0));
    CHECK(main.item(gain)->property("editing").toBool());
    CHECK(main.item(gain + ":down")->isVisible());
    CHECK(main.item(gain + ":text")->property("text").toString() == "1.0 dB");
    QTest::keyClick(main.window, Qt::Key_Return);
    MainWindow::settle();
    CHECK_FALSE(main.item(gain)->property("editing").toBool());
    CHECK(trackOf(main, vox).gainDb == Catch::Approx(1.0));
}
