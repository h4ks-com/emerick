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
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
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

TEST_CASE("the highlighted sound shows a preview button that auditions it without picking it") {
    const QTemporaryDir cache;
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    });
    const std::string id = main->rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id");
    main->rig.nav.setSelectedTrack(QString::fromStdString(id));
    std::vector<Json> auditions;
    QObject::connect(&main->rig.registry, &Registry::actionRan,
                     [&](const std::string& action, const Json& params, ActionSource) {
                         if (action == "voices.audition") {
                             auditions.push_back(params);
                         }
                     });

    QQuickItem* picker = main->item(QString::fromStdString("instrument.setParam:" + id + ":0"));
    REQUIRE(QMetaObject::invokeMethod(picker, "open"));
    for (const char letter : std::string{"violin"}) {
        QTest::keyClick(main->window, letter);
    }
    QQuickItem* preview = main->item(QString::fromStdString("voices.audition:" + id + ":40"));
    CHECK(preview->property("text").toString() == "Preview voice");
    main->click(preview);
    REQUIRE(auditions.size() == 1);
    CHECK(auditions[0].at("voice") == "Violin");
    CHECK(processUntil([&] { return main->rig.controller->engine().previewPlaying(); }));
    CHECK(picker->property("popup").value<QObject*>()->property("opened").toBool());
    CHECK(picker->property("displayText").toString() == "Acoustic Grand Piano");
    QTest::keyClick(main->window, Qt::Key_Escape);
    MainWindow::settle();
}

TEST_CASE("the inspector shows the General MIDI drum params only on a drum track and the Sound only on a melodic one") {
    const QTemporaryDir cache;
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    });
    const auto shows = [&](const std::string& track, int param) {
        return MainWindow::find(main->window->contentItem(),
                                QString::fromStdString("instrument.setParam:" + track + ":" + std::to_string(param))) !=
               nullptr;
    };
    constexpr int kSound = 0;
    constexpr int kDrumKit = 1;
    constexpr int kDrumsEverywhere = 4;
    const std::string keys = main->rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id");
    main->rig.nav.setSelectedTrack(QString::fromStdString(keys));
    REQUIRE(processUntil([&] { return shows(keys, kSound) && shows(keys, kDrumKit); }));

    const std::string clip = main->rig.run("midi.addClip", {{"track", keys}, {"at", 0}}).at("id");
    main->rig.run("midi.addNote", {{"clip", clip}, {"pitch", 60}, {"start", 0}});
    CHECK(
        processUntil([&] { return shows(keys, kSound) && !shows(keys, kDrumKit) && !shows(keys, kDrumsEverywhere); }));

    main->rig.run("instrument.setParam", {{"track", keys}, {"param", kDrumsEverywhere}, {"value", 1}});
    CHECK(processUntil([&] { return !shows(keys, kSound) && shows(keys, kDrumKit) && shows(keys, kDrumsEverywhere); }));

    const std::string drums = main->rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id");
    const std::string beat = main->rig.run("midi.addClip", {{"track", drums}, {"at", 0}}).at("id");
    main->rig.run("midi.addNote", {{"clip", beat}, {"pitch", 36}, {"start", 0}, {"channel", 10}});
    main->rig.nav.setSelectedTrack(QString::fromStdString(drums));
    CHECK(processUntil(
        [&] { return !shows(drums, kSound) && shows(drums, kDrumKit) && shows(drums, kDrumsEverywhere); }));
    MainWindow::settle();
}

TEST_CASE("the row below the last track adds each kind of track and stays out of the track list") {
    const QTemporaryDir cache;
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    });
    const auto& tracks = main->rig.controller->project().document().tracks;
    const auto lastKind = [&] { return tracks.empty() ? std::string{} : tracks.back().kind; };
    main->click(main->item("track.addAudio"));
    CHECK(processUntil([&] { return tracks.size() == 1 && lastKind() == "audio"; }));
    main->click(main->item("track.addMidi"));
    CHECK(processUntil([&] { return tracks.size() == 2 && lastKind() == "midi"; }));
    main->click(main->item("track.addBus"));
    CHECK(processUntil([&] { return tracks.size() == 3 && lastKind() == "bus"; }));
    QObject* instruments = main->menu("track.addInstrument:menu");
    QMetaObject::invokeMethod(menuItem(instruments, "General MIDI")->property("action").value<QObject*>(), "trigger");
    CHECK(processUntil([&] { return tracks.size() == 4 && tracks.back().instrument.has_value(); }));

    QQuickItem* list = main->item("tracks.list");
    QQuickItem* row = main->item("tracks.addRow");
    CHECK(list->property("count").toInt() == 4);
    CHECK(row->height() > main->rig.nav.trackHeight());
    CHECK(processUntil([&] { return row->y() == 4 * main->rig.nav.trackHeight(); }));
    MainWindow::settle();
}

TEST_CASE("the track menu lists SFZ and DX7 loading under their instrument, and a file entry asks for the file") {
    const QTemporaryDir cache;
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
        rig.registry.setSetting(kClapFoldersSetting, Json::array({bundledClapFolder(EMERICK_GM_APP_FOLDER).string()}));
    });
    const std::string id = main->rig.run("track.addInstrument", {{"plugin", kGeneralMidiPluginId}}).at("id");
    MainWindow::settle();
    main->click(main->item(QString::fromStdString("track.move:" + id)), Qt::RightButton);
    QObject* menu = main->menu("menu.trackContext");
    REQUIRE(processUntil([&] { return menu->property("opened").toBool(); }));
    const auto titles = itemTitles(menu);
    for (const char* moved : {"Load SFZ instrument", "List DX7 programs", "Load DX7 program"}) {
        CHECK(std::ranges::find(titles, QString{moved}) == titles.end());
    }

    const QString dexedMenu = QString{"instrument.load:"} + kDexedPluginId;
    const QString samplerMenu = QString{"instrument.load:"} + kSamplerPluginId;
    CHECK(itemTitles(main->menu(dexedMenu)) == std::vector<QString>{"Load Dexed", "Load DX7 program"});
    CHECK(itemTitles(main->menu(samplerMenu)) == std::vector<QString>{"Load Emerick Sampler", "Load SFZ instrument"});

    QVariantMap asked;
    QObject::connect(&main->rig.registry, &Registry::fileDialogRequested,
                     [&](const QVariantMap& request) { asked = request; });
    QMetaObject::invokeMethod(
        menuItem(main->menu(samplerMenu), "Load SFZ instrument")->property("action").value<QObject*>(), "trigger");
    CHECK(asked.value("id") == "sampler.load");
    CHECK(asked.value("params").toMap().value("track") == QString::fromStdString(id));

    main->click(main->item(QString::fromStdString("track.move:" + id)), Qt::RightButton);
    REQUIRE(processUntil([&] { return menu->property("opened").toBool(); }));
    QMetaObject::invokeMethod(menuItem(main->menu(dexedMenu), "Load Dexed")->property("action").value<QObject*>(),
                              "trigger");
    CHECK(main->rig.controller->project().track(model::TrackId{id})->instrument->pluginId == kDexedPluginId);
    QTest::keyClick(main->window, Qt::Key_Escape);
    MainWindow::settle();
}

TEST_CASE("the track menu shows only the entries that act on the clicked track's kind") {
    const QTemporaryDir cache;
    PluginCatalog catalog(
        {.cacheFolder = cache.path().toStdString(), .scanner = EMERICK_PLUGIN_SCANNER, .folders = {}});
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerPluginActions(rig.registry, rig.nav, catalog);
        registerInstrumentActions(rig.registry, *rig.controller, rig.nav, catalog);
    });
    const auto shownTitles = [&](const std::string& track) {
        main->click(main->item(QString::fromStdString("track.move:" + track)), Qt::RightButton);
        QObject* menu = main->menu("menu.trackContext");
        REQUIRE(processUntil([&] { return menu->property("opened").toBool(); }));
        std::vector<QString> titles;
        for (int index = 0; index < menu->property("count").toInt(); ++index) {
            QQuickItem* item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, index));
            if (item->isVisible()) {
                titles.push_back(item->property("text").toString());
            }
        }
        QTest::keyClick(main->window, Qt::Key_Escape);
        MainWindow::settle();
        return titles;
    };
    const auto shows = [](const std::vector<QString>& titles, const char* title) {
        return std::ranges::find(titles, QString{title}) != titles.end();
    };
    const std::string audio = main->rig.run("track.addAudio").at("id");
    const std::string midi = main->rig.run("track.addMidi").at("id");
    MainWindow::settle();

    const auto audioTitles = shownTitles(audio);
    for (const char* title :
         {"Load instrument", "Remove instrument", "Show instrument editor", "List instrument settings"}) {
        CHECK_FALSE(shows(audioTitles, title));
    }
    CHECK(shows(audioTitles, "Add send"));
    CHECK(shows(audioTitles, "Set input for all audio tracks"));

    const auto midiTitles = shownTitles(midi);
    for (const char* title : {"Add send", "Set track output bus", "Set input for all audio tracks", "Remove instrument",
                              "Clear automation lane"}) {
        CHECK_FALSE(shows(midiTitles, title));
    }
    CHECK(shows(midiTitles, "Load instrument"));
    CHECK(shows(midiTitles, "Arm track for recording"));
}
