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
