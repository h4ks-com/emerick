#include <QTemporaryDir>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

TEST_CASE("the MIDI input picker lists the devices of other tracks, and picking one takes its MIDI") {
    const QTemporaryDir cache;
    // The test folder holds a bundle that hangs, so a short timeout keeps the scan quick.
    PluginCatalog catalog({.cacheFolder = cache.path().toStdString(),
                           .scanner = EMERICK_PLUGIN_SCANNER,
                           .folders = {},
                           .timeout = std::chrono::milliseconds(500)});
    // We never delete the window, because destroying Qt Quick during static teardown crashes.
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({EMERICK_TEST_CLAP_FOLDER}));
    });
    const std::string device =
        main->rig.run("track.addInstrument", {{"plugin", "org.emerick.test.sine"}, {"name", "Style"}}).at("id");
    const std::string keys = main->rig.run("track.addMidi", {{"name", "Keys"}}).at("id");
    main->rig.nav.setSelectedTrack(QString::fromStdString(keys));

    QQuickItem* picker = main->item(QString::fromStdString("track.setMidiIn:" + keys));
    CHECK(picker->property("toolTip").toString() == "Set track MIDI input");
    REQUIRE(QMetaObject::invokeMethod(picker, "open"));
    for (const char letter : std::string{"style"}) {
        QTest::keyClick(main->window, letter);
    }
    QTest::keyClick(main->window, Qt::Key_Return);
    CHECK(processUntil([&] {
        return main->rig.controller->project().track(model::TrackId{keys})->midiIn.sourceTrack.value == device;
    }));
    CHECK(processUntil([&] { return picker->property("displayText").toString() == "Style / Test Sine"; }));
    CHECK(main->item(QString::fromStdString("track.setMidiIn:" + keys + ":channel")) != nullptr);

    main->rig.nav.setSelectedTrack(QString::fromStdString(device));
    QQuickItem* devicePicker = main->item(QString::fromStdString("track.setMidiIn:" + device));
    const QVariantList entries = devicePicker->property("model").toList();
    CHECK(std::ranges::none_of(
        entries, [](const QVariant& entry) { return entry.toMap().value("name").toString() == "Style / Test Sine"; }));
    MainWindow::settle();
}
