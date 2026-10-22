#include <QStringList>
#include <QtLogging>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QStringList& warnings() {
    static QStringList messages;
    return messages;
}

// Collects every Qt warning, so a test can fail on one such as a polish loop.
void collect(QtMsgType /*type*/, const QMessageLogContext& /*context*/, const QString& message) {
    warnings().append(message);
}

QString qid(const std::string& id) { return QString::fromStdString(id); }

void typeText(const MainWindow& main, const QString& text) {
    for (const QChar letter : text) {
        QTest::keyClick(main.window, letter.toLatin1());
    }
}

const model::Track& trackOf(const MainWindow& main, const std::string& id) {
    return *main.rig.controller->project().track(model::TrackId{id});
}

// Clicks a picker, types the filter and presses Enter.
void pick(MainWindow& main, const QString& picker, const QString& filter) {
    main.click(main.item(picker));
    REQUIRE(processUntil(
        [&] { return main.item(picker)->property("popup").value<QObject*>()->property("opened").toBool(); }));
    typeText(main, filter);
    QTest::keyClick(main.window, Qt::Key_Return);
    processUntil([] { return false; }, 50);
}

}  // namespace

TEST_CASE("the device picker filters as you type, picks with Enter and shows the device name") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    warnings().clear();
    const QtMessageHandler previous = qInstallMessageHandler(collect);

    const QString picker = "track.setAudioIn:" + qid(vox);
    main.click(main.item(picker));
    QObject* popup = main.item(picker)->property("popup").value<QObject*>();
    REQUIRE(processUntil([&] { return popup->property("opened").toBool(); }));
    typeText(main, "motu");
    CHECK(main.item(picker)->property("matches").toList().size() == 1);
    processUntil([] { return false; }, 100);
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(processUntil([&] { return !popup->property("opened").toBool(); }));
    qInstallMessageHandler(previous);

    CHECK(trackOf(main, vox).audioIn.deviceId == kMotuId);
    CHECK(trackOf(main, vox).audioIn.channels == std::vector{1, 2});
    CHECK(main.item(picker + ":search")->property("text").toString() == "MOTU M2");
    CHECK(warnings().filter("polish() loop").isEmpty());
}

TEST_CASE("clicking a device in the picker's list picks it") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    const QString picker = "track.setAudioIn:" + qid(vox);
    main.click(main.item(picker));
    QObject* popup = main.item(picker)->property("popup").value<QObject*>();
    REQUIRE(processUntil([&] { return popup->property("opened").toBool(); }));
    typeText(main, "motu");
    processUntil([] { return false; }, 100);

    auto* list = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>());
    REQUIRE(list != nullptr);
    QQuickItem* row = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, 0));
    REQUIRE(row != nullptr);
    main.click(row);
    CHECK(processUntil([&] { return !popup->property("opened").toBool(); }));

    CHECK(trackOf(main, vox).audioIn.deviceId == kMotuId);
    MainWindow::settle();
}

TEST_CASE("the channel pickers set only the channels of the route") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    pick(main, "track.setAudioOut:" + qid(vox) + ":channel", "2");
    CHECK(trackOf(main, vox).audioOut.channels == std::vector{2});
    CHECK(trackOf(main, vox).audioOut.deviceId == "interface");

    const std::string keys = main.rig.run("track.addMidi").at("id");
    pick(main, "track.setMidiIn:" + qid(keys) + ":channel", "9");
    CHECK(trackOf(main, keys).midiIn.channel == 9);
    CHECK(trackOf(main, keys).midiIn.portId == kMidiPort);
    pick(main, "track.setMidiOut:" + qid(keys), "key");
    CHECK(trackOf(main, keys).midiOut.portId == kMidiPort);
    CHECK(trackOf(main, keys).midiOut.channel == 0);
}

TEST_CASE("the track context menu lists registry actions and its Delete track removes the track with undo") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    processUntil([] { return false; }, 50);
    main.click(main.item("track.move:" + qid(vox)), Qt::RightButton);
    QObject* menu = main.menu("menu.trackContext");
    REQUIRE(processUntil([&] { return menu->property("opened").toBool(); }));
    CHECK(itemTitles(menu) == registryTitles(main.rig.registry, kTrackContextMenu));
    CHECK(main.rig.nav.trackMenu() == qid(vox));

    QMetaObject::invokeMethod(menuItem(menu, "Delete track")->property("action").value<QObject*>(), "trigger");
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) == nullptr);
    main.rig.run("project.undo");
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) != nullptr);
    MainWindow::settle();
}

TEST_CASE("Backspace and Delete remove the selected clips, or the selected track when no clip is selected") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    main.rig.run("selection.set", {{"clips", {clip}}});
    processUntil([] { return false; }, 50);

    QTest::keyClick(main.window, Qt::Key_Delete);
    CHECK_FALSE(main.rig.controller->project().findClip(model::ClipId{clip}).has_value());
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) != nullptr);

    main.rig.run("track.select", {{"track", vox}});
    QTest::keyClick(main.window, Qt::Key_Backspace);
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) == nullptr);
    main.rig.run("project.undo");
    main.rig.run("track.select", {{"track", vox}});
    QTest::keyClick(main.window, Qt::Key_Delete);
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) == nullptr);
    MainWindow::settle();
}

TEST_CASE("marker keys add and visit markers, the list locates, a double click renames and Delete removes") {
    MainWindow& main = MainWindow::instance();
    const core::Ticks bar = main.rig.controller->ticksPerBar();
    main.rig.run("transport.locate", {{"position", 2 * bar}});
    const auto& markers = main.rig.controller->project().document().markers;
    const auto before = markers.size();

    QTest::keyClick(main.window, Qt::Key_Apostrophe, Qt::AltModifier);
    REQUIRE(markers.size() == before + 1);
    const std::string id = main.rig.nav.selectedMarker().toStdString();
    CHECK(model::findMarker(main.rig.controller->project().document(), model::MarkerId{id})->at == 2 * bar);
    main.rig.run("transport.locate", {{"position", 0}});
    QTest::keyClick(main.window, Qt::Key_Period, Qt::AltModifier);
    CHECK(main.rig.controller->positionTicks() == 2 * bar);

    main.rig.run("transport.locate", {{"position", 0}});
    main.click(main.item("marker.goto:" + qid(id)));
    CHECK(processUntil([&] { return main.rig.controller->positionTicks() == 2 * bar; }));

    QQuickItem* flag = main.item("marker.move:" + qid(id));
    QTest::mouseDClick(main.window, Qt::LeftButton, Qt::NoModifier,
                       flag->mapToScene({flag->width() / 2, flag->height() / 2}).toPoint());
    REQUIRE(processUntil([&] { return main.item("marker.rename:" + qid(id))->hasActiveFocus(); }));
    typeText(main, "Bridge");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(model::findMarker(main.rig.controller->project().document(), model::MarkerId{id})->name == "Bridge");

    main.rig.run("marker.select", {{"marker", id}});
    processUntil([] { return false; }, 50);
    QTest::keyClick(main.window, Qt::Key_Delete);
    CHECK(markers.size() == before);
    MainWindow::settle();
}

TEST_CASE("the settings close button and Escape close settings") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("settings.open");
    REQUIRE(main.rig.nav.page() == Page::Settings);
    CHECK(main.item("settings.title")->property("text").toString() == "General");
    main.click(main.item("settings.close"));
    CHECK(processUntil([&] { return main.rig.nav.page() == Page::Main; }));

    main.rig.run("settings.open");
    processUntil([] { return false; }, 50);
    QTest::keyClick(main.window, Qt::Key_Escape);
    CHECK(processUntil([&] { return main.rig.nav.page() == Page::Main; }));
    MainWindow::settle();
}

TEST_CASE("Logic keys move the playhead by bars, select tracks and mute the selected track") {
    MainWindow& main = MainWindow::instance();
    const core::Ticks bar = main.rig.controller->ticksPerBar();
    main.rig.run("transport.locate", {{"position", 0}});
    const std::string first = main.rig.run("track.addMidi").at("id");
    const std::string second = main.rig.run("track.addMidi").at("id");

    QTest::keyClick(main.window, Qt::Key_Period);
    CHECK(main.rig.controller->positionTicks() == bar);
    QTest::keyClick(main.window, Qt::Key_Period, Qt::ShiftModifier);
    CHECK(main.rig.controller->positionTicks() == 9 * bar);
    QTest::keyClick(main.window, Qt::Key_Comma);
    CHECK(main.rig.controller->positionTicks() == 8 * bar);
    QTest::keyClick(main.window, Qt::Key_Home);
    CHECK(main.rig.controller->positionTicks() == 0);

    CHECK(main.rig.nav.selectedTrack() == qid(second));
    QTest::keyClick(main.window, Qt::Key_Up);
    CHECK(main.rig.nav.selectedTrack() == qid(first));
    QTest::keyClick(main.window, Qt::Key_M);
    CHECK(trackOf(main, first).mute);
    QTest::keyClick(main.window, Qt::Key_M);
    CHECK_FALSE(trackOf(main, first).mute);
}

TEST_CASE("Key Commands records a new shortcut that then runs the action") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("settings.show.keys");
    main.click(main.item("keys.setShortcut:transport.gotoEnd"));
    QTest::keyClick(main.window, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(main.rig.registry.action("transport.gotoEnd")->shortcut == "Ctrl+Shift+E");
    main.rig.run("settings.close");

    main.rig.run("transport.locate", {{"position", "3.1.0"}});
    REQUIRE(main.rig.controller->endTicks() != main.rig.controller->positionTicks());
    processUntil([] { return false; }, 50);
    QTest::keyClick(main.window, Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(main.rig.controller->positionTicks() == main.rig.controller->endTicks());
    MainWindow::settle();
}

TEST_CASE("Space plays and pauses after clicking any control, and Space and X type into a focused text field") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    main.rig.run("track.setGain", {{"track", vox}, {"db", 0.0}});
    processUntil([] { return false; }, 50);
    const auto spaceToggles = [&](const QString& clicked) {
        INFO(clicked.toStdString());
        if (main.rig.controller->playing()) {
            main.rig.run("transport.stop");
        }
        QTest::keyClick(main.window, Qt::Key_Space);
        CHECK(processUntil([&] { return main.rig.controller->playing(); }));
        main.rig.run("transport.stop");
    };
    for (const QString& name :
         {QString{"view.toggleInspector"}, "track.setMute:" + qid(vox), "track.move:" + qid(vox),
          "track.setGain:" + qid(vox), QString{"metronome.toggle"}, QString{"transport.togglePlay"}}) {
        main.click(main.item(name));
        processUntil([] { return false; }, 50);
        spaceToggles(name);
    }
    main.drag(main.item("track.setGain:" + qid(vox) + ":db:drag"), {30, 0});
    spaceToggles("gain field");

    main.click(main.item("track.setGain:" + qid(vox) + ":db"));
    REQUIRE(processUntil([&] { return main.item("track.setGain:" + qid(vox) + ":db")->property("editing").toBool(); }));
    main.click(main.item("track.move:" + qid(vox)));
    CHECK_FALSE(main.item("track.setGain:" + qid(vox) + ":db")->property("editing").toBool());
    spaceToggles("track header after typing into the gain field");

    main.rig.run("track.beginRename", {{"track", vox}});
    QQuickItem* rename = main.item("track.rename:" + qid(vox));
    REQUIRE(processUntil([&] { return rename->hasActiveFocus(); }));
    QTest::keyClick(main.window, Qt::Key_End);
    QTest::keyClick(main.window, Qt::Key_Space);
    processUntil([] { return false; }, 50);
    CHECK_FALSE(main.rig.controller->playing());
    CHECK(rename->property("text").toString().endsWith(" "));
    QTest::keyClick(main.window, Qt::Key_X);
    processUntil([] { return false; }, 50);
    CHECK_FALSE(main.rig.nav.mixerOpen());
    CHECK(rename->property("text").toString().endsWith(" x"));
    QTest::keyClick(main.window, Qt::Key_Escape);
    MainWindow::settle();
}

TEST_CASE("the default input picker in Settings sets the default and its button applies it to every audio track") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    main.rig.run("settings.show.audio");
    processUntil([] { return false; }, 50);

    pick(main, "audio.defaultInput", "motu");
    CHECK(main.rig.registry.settingValue("audio.defaultInput") == Json{{"device", kMotuId}, {"channels", {1, 2}}});
    pick(main, "audio.defaultInput:channel", "2");
    CHECK(main.rig.registry.settingValue("audio.defaultInput").at("channels") == Json{2});
    main.click(main.item("track.setAllAudioIn"));
    main.rig.registry.setSetting("audio.defaultInput", {{"device", "interface"}, {"channels", {1, 2}}});
    main.rig.run("settings.show.general");
    main.rig.run("settings.close");

    CHECK(trackOf(main, vox).audioIn.deviceId == kMotuId);
    CHECK(trackOf(main, vox).audioIn.channels == std::vector{2});
    MainWindow::settle();
}
