#include <catch2/catch_test_macros.hpp>
#include <string>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

TEST_CASE("the inspector's analyze button renders the track off the UI thread and shows its readout") {
    MainWindow& main = MainWindow::instance();
    const std::string id = main.rig.run("track.addAudio", {{"name", "Vox"}}).at("id");
    writeConstant(main.rig.path("pad.wav"), 48000, 0.25F);
    main.rig.run("clip.import", {{"track", id}, {"path", main.rig.path("pad.wav").string()}});
    main.rig.nav.setSelectedTrack(QString::fromStdString(id));
    const QString name = QString::fromStdString("track.analyze:" + id);

    QQuickItem* button = main.item(name);
    CHECK(button->property("text").toString() == "Analyze track");
    CHECK(MainWindow::find(main.window->contentItem(), name + ":readout") == nullptr);

    main.click(button);
    CHECK(processUntil([&] { return MainWindow::find(main.window->contentItem(), name + ":readout") != nullptr; }));
    CHECK(main.item(name + ":LUFS")->property("text").toString().startsWith("-"));
    CHECK(main.item(name + ":RMS")->property("text").toString().startsWith("-12.0"));
    MainWindow::settle();
}

TEST_CASE("the mixer's analyze button shows each strip's active RMS only after it ran") {
    MainWindow& main = MainWindow::instance();
    const std::string id = main.rig.run("track.addAudio", {{"name", "Bass"}}).at("id");
    writeConstant(main.rig.path("bass.wav"), 48000, 0.5F);
    main.rig.run("clip.import", {{"track", id}, {"path", main.rig.path("bass.wav").string()}});
    main.rig.nav.setMixerOpen(true);
    const QString reading = QString::fromStdString("tracks.analyze:" + id);

    QQuickItem* button = main.item("tracks.analyze");
    CHECK(button->property("text").toString() == "Analyze all tracks");
    CHECK(MainWindow::find(main.item("mixer"), reading) == nullptr);

    main.click(button);
    CHECK(processUntil([&] { return MainWindow::find(main.item("mixer"), reading) != nullptr; }));
    CHECK(main.item(reading)->property("text").toString().startsWith("-6.0"));
    main.rig.nav.setMixerOpen(false);
    MainWindow::settle();
}
