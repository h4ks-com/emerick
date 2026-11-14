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
    CHECK(trackOf(main, vox).audioIn.channels == std::vector{1});
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

    QObject* colors = main.menu("track.setColor");
    CHECK(menuItem(colors, "Rose")->property("swatch").toInt() == 3);
    QMetaObject::invokeMethod(colors, "aboutToShow");
    CHECK(menuItem(colors, "Teal")->property("marked").toBool());
    CHECK_FALSE(menuItem(colors, "Rose")->property("marked").toBool());
    CHECK(menuItem(main.menu("track.setIcon"), "Guitar")->property("iconName").toString() == "guitar");
    CHECK(menuItem(menu, "Delete track")->property("swatch").toInt() == -1);

    QMetaObject::invokeMethod(menuItem(menu, "Delete track")->property("action").value<QObject*>(), "trigger");
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) == nullptr);
    main.rig.run("project.undo");
    CHECK(main.rig.controller->project().track(model::TrackId{vox}) != nullptr);
    MainWindow::settle();
}

TEST_CASE(
    "a right click on a clip selects it and its track and opens the clip menu, and a click below the tracks deselects "
    "it") {
    MainWindow& main = MainWindow::instance();
    const std::string vox = main.rig.run("track.addAudio").at("id");
    writeConstant(main.rig.path("ones.wav"), 48000, 1.0F);
    const std::string clip =
        main.rig.run("clip.import", {{"track", vox}, {"path", main.rig.path("ones.wav").string()}}).at("id");
    main.rig.run("selection.clear");
    main.rig.run("track.addAudio");
    processUntil([] { return false; }, 50);

    main.click(main.item("clip.move:" + qid(clip)), Qt::RightButton);
    CHECK(main.rig.nav.selectedTrack() == qid(vox));
    QObject* clipMenu = main.menu("menu.clipContext");
    CHECK(processUntil([&] { return clipMenu->property("opened").toBool(); }));
    CHECK_FALSE(main.menu("menu.laneContext")->property("visible").toBool());
    CHECK(main.rig.nav.selectedClips() == QStringList{qid(clip)});
    QMetaObject::invokeMethod(clipMenu, "close");
    REQUIRE(processUntil([&] { return !clipMenu->property("visible").toBool(); }));

    constexpr qreal kInset = 40;
    QQuickItem* tracks = main.item("tracks");
    const QPointF below = tracks->mapToScene({tracks->width() - kInset, tracks->height() - kInset});
    QTest::mouseClick(main.window, Qt::LeftButton, Qt::NoModifier, below.toPoint());
    CHECK(main.rig.nav.selectedClips().isEmpty());
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
    main.rig.run("transport.locate", {{"at", 2 * bar}});
    const auto& markers = main.rig.controller->project().document().markers;
    const auto before = markers.size();

    QTest::keyClick(main.window, Qt::Key_M, Qt::AltModifier);
    REQUIRE(markers.size() == before + 1);
    const std::string id = main.rig.nav.selectedMarker().toStdString();
    CHECK(model::findMarker(main.rig.controller->project().document(), model::MarkerId{id})->at == 2 * bar);
    main.rig.run("transport.locate", {{"at", 0}});
    QTest::keyClick(main.window, Qt::Key_Period, Qt::AltModifier);
    CHECK(main.rig.controller->positionTicks() == 2 * bar);

    main.rig.run("transport.locate", {{"at", 0}});
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

TEST_CASE("a clip header shows its track and take until the clip is renamed in place") {
    MainWindow& main = MainWindow::instance();
    main.rig.nav.setScrollTicks(0);
    const std::string keys = main.rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    const std::string clip = main.rig.run("midi.addClip", {{"track", keys}, {"at", 0}}).at("id");
    MainWindow::settle();
    QQuickItem* name = main.item("clip.name:" + qid(clip));
    CHECK(name->property("text").toString() == "Keys   Take 1");

    main.rig.run("clip.beginRename", {{"clip", clip}});
    REQUIRE(processUntil([&] { return main.item("clip.rename:" + qid(clip))->hasActiveFocus(); }));
    typeText(main, "Intro");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(processUntil([&] { return name->property("text").toString() == "Intro"; }));
    CHECK(main.rig.nav.renamingClip().isEmpty());
    main.rig.run("project.undo");
    CHECK(processUntil([&] { return name->property("text").toString() == "Keys   Take 1"; }));
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
    main.rig.run("transport.locate", {{"at", 0}});
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

    main.rig.run("transport.locate", {{"at", "3.1.0"}});
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
    CHECK(main.rig.registry.settingValue("audio.defaultInput") == Json{{"device", kMotuId}, {"channels", {1}}});
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

TEST_CASE("Cmd-clicking track headers selects both, and the inspector shows Mixed and sets every selected track") {
    MainWindow& main = MainWindow::instance();
    const std::string first = main.rig.run("track.addAudio").at("id");
    const std::string second = main.rig.run("track.addAudio").at("id");
    main.rig.run("track.setAudioIn", {{"track", first}, {"device", kMotuId}});
    MainWindow::settle();
    main.click(main.item("track.move:" + qid(first)));
    QQuickItem* header = main.item("track.move:" + qid(second));
    const QPointF at = header->mapToScene({header->width() / 2, header->height() / 2});
    main.send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::ControlModifier);
    main.send(QEvent::MouseButtonRelease, at, Qt::NoButton, Qt::ControlModifier);
    MainWindow::settle();
    CHECK(main.rig.nav.selectedTracks() == QStringList{qid(first), qid(second)});
    CHECK(main.item("inspector.trackTitle")->property("text").toString() == "2 tracks");

    const QString picker = "track.setAudioIn:" + qid(second);
    CHECK(main.item(picker)->property("displayText").toString() == "Mixed");
    pick(main, picker, "motu");
    CHECK(trackOf(main, first).audioIn.deviceId == kMotuId);
    CHECK(trackOf(main, second).audioIn.deviceId == kMotuId);
    CHECK(main.item(picker)->property("displayText").toString() == "MOTU M2");

    main.click(main.item("track.setMute:" + qid(first)));
    CHECK(trackOf(main, first).mute);
    CHECK(trackOf(main, second).mute);
    main.rig.run("project.undo");
    CHECK_FALSE(trackOf(main, second).mute);

    main.click(main.item("track.move:" + qid(first)));
    CHECK(main.rig.nav.selectedTracks() == QStringList{qid(first)});
    QTest::keyClick(main.window, Qt::Key_A, Qt::ControlModifier);
    CHECK(main.rig.nav.selectedTracks().size() == std::ssize(main.rig.controller->project().document().tracks));
    main.rig.run("track.select", {{"track", first}});
    MainWindow::settle();
}

TEST_CASE("the marker lane buttons add a marker at the playhead, step between markers and toggle the list") {
    MainWindow& main = MainWindow::instance();
    const core::Ticks bar = main.rig.controller->ticksPerBar();
    const auto& markers = main.rig.controller->project().document().markers;
    const auto before = markers.size();
    main.rig.run("transport.locate", {{"at", 5 * bar}});
    main.click(main.item("marker.add:lane"));
    REQUIRE(markers.size() == before + 1);
    CHECK(model::findMarker(main.rig.controller->project().document(),
                            model::MarkerId{main.rig.nav.selectedMarker().toStdString()})
              ->at == 5 * bar);
    main.rig.run("transport.locate", {{"at", 7 * bar}});
    main.click(main.item("marker.previous:lane"));
    CHECK(main.rig.controller->positionTicks() == 5 * bar);

    main.click(main.item("marker.toggleList:lane"));
    CHECK_FALSE(main.rig.nav.markerListOpen());
    main.click(main.item("marker.toggleList:lane"));
    CHECK(main.rig.nav.markerListOpen());
    main.rig.run("project.undo");
    MainWindow::settle();
}

TEST_CASE("dragging the inspector's edge resizes it within its limits and keeps the width as a setting") {
    MainWindow& main = MainWindow::instance();
    QQuickItem* inspector = main.item("inspector");
    const double width = inspector->width();
    main.drag(main.item("view.inspectorWidth"), QPointF{60, 0});
    CHECK(main.rig.registry.settingValue("view.inspectorWidth") == static_cast<int>(width) + 60);
    CHECK(inspector->width() == width + 60);
    main.drag(main.item("view.inspectorWidth"), QPointF{-2000, 0});
    CHECK(main.rig.registry.settingValue("view.inspectorWidth") == 200);
    main.rig.registry.setSetting("view.inspectorWidth", static_cast<int>(width));
    MainWindow::settle();
}

TEST_CASE("a long marker list leaves most of a short inspector to the track settings") {
    MainWindow& main = MainWindow::instance();
    const QSize size = main.window->size();
    for (int marker = 0; marker < 12; ++marker) {
        main.rig.run("marker.add", {{"at", (500 + marker) * 3840}});
    }
    main.rig.nav.setMarkerListOpen(true);
    main.rig.nav.setConsoleOpen(true);
    main.window->resize(size.width(), 700);
    MainWindow::settle();

    QQuickItem* inspector = main.item("inspector");
    CHECK(main.item("inspector.scroll")->height() > inspector->height() / 2);

    main.window->resize(size);
    main.rig.nav.setConsoleOpen(false);
    main.rig.nav.setMarkerListOpen(false);
    MainWindow::settle();
}

TEST_CASE("an action the UI runs that fails shows its error in the notice dialog") {
    MainWindow& main = MainWindow::instance();
    const std::string keys = main.rig.run("track.addMidi").at("id");
    const std::string bus = main.rig.run("track.addBus").at("id");
    main.rig.registry.runAction("send.add", {{"track", qid(keys)}, {"bus", qid(bus)}});
    QObject* notice = main.menu("dialog.notice");
    CHECK(processUntil([&] { return notice->property("visible").toBool(); }));
    CHECK(notice->property("title").toString() == "Add send");
    CHECK(notice->property("text").toString().contains("audio"));
    QMetaObject::invokeMethod(notice, "close");
    MainWindow::settle();
}
