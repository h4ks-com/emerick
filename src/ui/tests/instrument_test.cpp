#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

TEST_CASE("the inspector shows the Sound picker of a General MIDI track, and picking a sound sets it") {
    const QTemporaryDir cache;
    PluginCatalog catalog({.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_CLAP_SCANNER, .folders = {}});
    // We never delete the window, because destroying Qt Quick during static teardown crashes.
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    });
    const std::string id = main->rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id");
    main->rig.nav.setSelectedTrack(QString::fromStdString(id));
    const auto soundText = [&] {
        return main->rig.run("instrument.params", {{"track", id}}).at("params")[0].at("text").get<std::string>();
    };

    const QString pickerName = QString::fromStdString("instrument.setParam:" + id + ":0");
    QQuickItem* picker = main->item(pickerName);
    CHECK(picker->property("displayText").toString() == "Acoustic Grand Piano");
    REQUIRE(QMetaObject::invokeMethod(picker, "open"));
    for (const char letter : std::string{"violin"}) {
        QTest::keyClick(main->window, letter);
    }
    QTest::keyClick(main->window, Qt::Key_Return);
    CHECK(processUntil([&] { return soundText() == "Violin"; }));
    CHECK(processUntil([&] { return picker->property("displayText").toString() == "Violin"; }));
    CHECK(main->item(pickerName) == picker);

    main->rig.run("project.undo");
    CHECK(processUntil([&] { return picker->property("displayText").toString() == "Acoustic Grand Piano"; }));
    MainWindow::settle();
}
