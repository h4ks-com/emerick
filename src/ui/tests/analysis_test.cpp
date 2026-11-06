#include <QTest>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <format>
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

TEST_CASE("inspecting the mix from the UI lists each issue, and a click selects its track and goes to it") {
    MainWindow& main = MainWindow::instance();
    const std::string id = main.rig.run("track.addAudio", {{"name", "Offset"}}).at("id");
    writeConstant(main.rig.path("offset.wav"), 48000, 0.25F);
    main.rig.run("clip.import", {{"track", id}, {"path", main.rig.path("offset.wav").string()}, {"at", "3.1.0"}});
    main.rig.nav.setSelectedTrack({});
    const Json issues = main.rig.run("mix.inspect").at("issues");
    const auto issue = std::ranges::find(issues, Json(id), [](const Json& entry) { return entry.at("track"); });
    REQUIRE(issue != issues.end());
    const QString row = QStringLiteral("transport.locate:%1").arg(issue - issues.begin());

    main.rig.registry.activate("mix.inspect");
    CHECK(main.item(row)->property("text").toString().contains(
        QString::fromStdString(std::format("Offset, {} to ", issue->at("from").at("bbt").get<std::string>()))));
    main.click(main.item(row));
    CHECK(main.rig.nav.selectedTrack() == QString::fromStdString(id));
    const ProjectController& controller = *main.rig.controller;
    CHECK(controller.project().document().tempo.toBarBeatTick(controller.positionTicks()).format() ==
          issue->at("from").at("bbt").get<std::string>());
    QTest::keyClick(main.window, Qt::Key_Escape);
    CHECK(processUntil([&] { return MainWindow::find(main.window->contentItem(), "issues") == nullptr; }));
    main.rig.run("transport.locate", {{"at", 0}});
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
