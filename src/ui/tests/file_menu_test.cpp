#include <catch2/catch_test_macros.hpp>

#include "emerick/model/json.hpp"
#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

TEST_CASE("the File menu and the header's project menu list their registry actions in order") {
    MainWindow& main = MainWindow::instance();
    const auto file = registryTitles(main.rig.registry, "File");
    REQUIRE(file.size() > 6);
    CHECK(processUntil([&] { return itemTitles(main.menu("menu.file")) == file; }));
    CHECK(main.menu("project.saveAs") != nullptr);
    CHECK(main.menu("project.openRecent")->property("title").toString() == "Open recent project");

    const auto project = registryTitles(main.rig.registry, "Project");
    CHECK(project == std::vector<QString>{"New project", "Open project", "Open recent project", "Save project as",
                                          "Export for Logic",
                                          main.rig.registry.action("project.reveal")->title.c_str()});
    main.rig.run("project.showMenu");
    QObject* header = main.menu("menu.project");
    CHECK(processUntil([&] { return header->property("opened").toBool(); }));
    CHECK(itemTitles(header) == project);
    main.rig.run("project.showMenu");
    CHECK(processUntil([&] { return !header->property("visible").toBool(); }));
}

TEST_CASE("opening a project with a missing file offers its issues, and an issue selects its clip") {
    MainWindow& main = MainWindow::instance();
    model::Document document;
    model::AudioClip clip;
    clip.file = "audio/gone.wav";
    clip.length = document.tempo.ticksPerBar();
    model::Track track;
    track.name = "Vox";
    track.clips = {clip};
    document.tracks = {track};
    const auto bundle = main.rig.path("Missing.erk");
    model::saveProject(document, bundle);

    main.rig.run("project.open", {{"path", bundle.string()}});
    main.click(main.item("project.health"));
    CHECK(main.rig.nav.healthOpen());
    main.click(main.item(QString::fromStdString("selection.set:" + clip.id.value)));
    CHECK(main.rig.nav.selectedClips() == QStringList{QString::fromStdString(clip.id.value)});
    CHECK(processUntil([&] { return !main.rig.nav.healthOpen(); }));
    MainWindow::settle();
}

TEST_CASE("the File menu's shortcut saves the project and the title drops the edited dot") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("track.addMidi");
    REQUIRE(processUntil([&] { return main.window->title().endsWith("•"); }));
    QTest::keyClick(main.window, Qt::Key_S, Qt::ControlModifier);
    CHECK(processUntil([&] { return !main.rig.controller->edited(); }, 1000));
    CHECK(main.window->title() == "Untitled 1");
}
