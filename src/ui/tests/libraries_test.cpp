#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "main_window.hpp"

using namespace emerick::app;
using namespace emerick::app::test;

TEST_CASE("the libraries button opens a row per library, and Remove deletes an installed one on a second click") {
    MainWindow& main = MainWindow::instance();
    std::filesystem::create_directories(main.rig.path("libraries") / "big-rusty-drums");
    main.click(main.item("library.open"));
    CHECK(main.rig.nav.librariesOpen());
    CHECK(main.item("library.install:salamander-grand-piano") != nullptr);
    CHECK(MainWindow::find(main.window->contentItem(), "library.install:big-rusty-drums") == nullptr);
    CHECK(MainWindow::find(main.window->contentItem(), "library.cancel:big-rusty-drums") == nullptr);

    QQuickItem* remove = main.item("library.remove:big-rusty-drums");
    main.click(remove);
    CHECK(std::filesystem::exists(main.rig.path("libraries") / "big-rusty-drums"));
    CHECK(remove->property("text").toString() == "Confirm remove");
    main.click(remove);
    CHECK(processUntil([&] { return !std::filesystem::exists(main.rig.path("libraries") / "big-rusty-drums"); }));
    CHECK(main.item("library.install:big-rusty-drums") != nullptr);
    main.rig.nav.setLibrariesOpen(false);
    MainWindow::settle();
}

TEST_CASE("opening the libraries at one library marks its row") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("library.open", {{"library", "vsco2-ce"}});
    CHECK(main.item("library.row:vsco2-ce")->property("color") !=
          main.item("library.row:big-rusty-drums")->property("color"));
    main.rig.nav.setLibrariesOpen(false);
    MainWindow::settle();
}
