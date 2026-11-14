#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

QString qid(const std::string& id) { return QString::fromStdString(id); }

QObject* openMenu(const MainWindow& main, QQuickItem* button) {
    main.click(button);
    auto* menu = button->property("menu").value<QObject*>();
    REQUIRE(menu != nullptr);
    REQUIRE(processUntil([&] { return menu->property("opened").toBool(); }));
    return menu;
}

void trigger(QObject* menu, const QString& title) {
    QObject* item = menuItem(menu, title);
    if (QObject* action = item->property("action").value<QObject*>()) {
        QMetaObject::invokeMethod(action, "trigger");
    } else {
        QMetaObject::invokeMethod(item, "triggered");
    }
    REQUIRE(processUntil([&] { return !menu->property("visible").toBool(); }));
}

}  // namespace

TEST_CASE("the inspector's preset button saves the inserts under a typed name and loads them on another track") {
    const QTemporaryDir cache;
    PluginCatalog catalog({.cacheFolder = cache.path().toStdString(), .scanner = {}, .folders = {}});
    // We never delete the window, because destroying Qt Quick during static teardown crashes.
    auto* main = new MainWindow([&catalog](ProjectRig& rig) {
        registerTrackPresetActions(rig.registry, *rig.controller, rig.nav, catalog, rig.path("presets"));
    });
    const std::string vox = main->rig.run("track.addAudio").at("id");
    main->rig.run("insert.add", {{"track", vox}, {"type", "eq"}});
    main->rig.nav.setSelectedTrack(qid(vox));

    QQuickItem* voxButton = main->item("track.loadPreset:" + qid(vox));
    CHECK(voxButton->property("text").toString() == "Load preset");
    trigger(openMenu(*main, voxButton), "Save preset");
    QQuickItem* name = main->item("track.savePreset:" + qid(vox) + ":name");
    REQUIRE(processUntil([&] { return name->hasActiveFocus(); }));
    for (const char letter : std::string{"Clean"}) {
        QTest::keyClick(main->window, letter);
    }
    QTest::keyClick(main->window, Qt::Key_Return);
    CHECK(std::filesystem::exists(main->rig.path("presets/Clean.json")));
    CHECK(processUntil([&] { return voxButton->property("text").toString() == "Clean"; }));

    const std::string bass = main->rig.run("track.addAudio").at("id");
    main->rig.nav.setSelectedTrack(qid(bass));
    trigger(openMenu(*main, main->item("track.loadPreset:" + qid(bass))), "Clean");
    const model::Track& loaded = *main->rig.controller->project().track(model::TrackId{bass});
    REQUIRE(loaded.inserts.size() == 1);
    CHECK(loaded.inserts.front().type == "eq");
    CHECK(processUntil(
        [&] { return main->item("track.loadPreset:" + qid(bass))->property("text").toString() == "Clean"; }));
    MainWindow::settle();
}
