#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <string_view>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

void type(QWindow* window, std::string_view text) {
    for (const char c : text) {
        QTest::keyClick(window, c);
    }
}

const ConsoleEntry& lastInfo(const Console& console) {
    return *std::ranges::find(console.entries() | std::views::reverse, ConsoleLine::Action, &ConsoleEntry::kind);
}

}  // namespace

TEST_CASE("the palette runs text after > as Lua and shows its value inline") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("palette.open");
    QQuickItem* query = main.item("palette.query");
    REQUIRE(processUntil([&] { return query->hasActiveFocus(); }));
    type(main.window, ">1 + 2");
    QQuickItem* result = main.item("palette.luaResult");
    CHECK(result->property("text") == "Press Enter to run as Lua");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(processUntil([&] { return result->property("text") == "3"; }));
    CHECK(main.rig.nav.paletteOpen());
    main.rig.nav.setPaletteOpen(false);
    MainWindow::settle();
}

TEST_CASE("the console panel completes action ids, runs Lua and logs the action as Lua") {
    MainWindow& main = MainWindow::instance();
    const auto selected = main.rig.run("track.addMidi").at("id").get<std::string>();
    main.rig.nav.setSelectedTrack(QString::fromStdString(selected));
    main.rig.run("view.toggleConsole");
    QQuickItem* input = main.item("console.eval");
    REQUIRE(processUntil([&] { return input->hasActiveFocus(); }));

    type(main.window, "m");
    CHECK(!main.rig.controller->project().track(model::TrackId{selected})->mute);
    QTest::keyClick(main.window, Qt::Key_Backspace);

    const auto trackCount = main.rig.controller->project().document().tracks.size();
    type(main.window, "emerick.run(\"track.addau");
    QObject* completions = main.menu("console.completions");
    CHECK(processUntil([&] { return completions->property("visible").toBool(); }));
    QTest::keyClick(main.window, Qt::Key_Tab);
    CHECK(input->property("text") == "emerick.run(\"track.addAudio\"");
    type(main.window, ")");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(main.rig.controller->project().document().tracks.size() == trackCount + 1);
    CHECK(input->property("text").toString().isEmpty());
    CHECK(lastInfo(main.console).text == R"(emerick.run("track.addAudio"))");
    CHECK(lastInfo(main.console).source == ActionSource::Lua);
    CHECK(main.item("console.output")->property("text").toString().contains("emerick.run(\"track.addAudio\")"));

    QTest::keyClick(main.window, Qt::Key_Up);
    CHECK(input->property("text") == "emerick.run(\"track.addAudio\")");
    QTest::keyClick(main.window, Qt::Key_Down);
    CHECK(input->property("text").toString().isEmpty());

    type(main.window, "local x = 4");
    QTest::keyClick(main.window, Qt::Key_Return, Qt::ShiftModifier);
    type(main.window, "return x * 2");
    CHECK(input->property("text") == "local x = 4\nreturn x * 2");
    QTest::keyClick(main.window, Qt::Key_Return);
    CHECK(main.console.entries().back().text == "8");

    QTest::keyClick(main.window, Qt::Key_L, Qt::ControlModifier);
    CHECK(main.console.entries().empty());
    main.rig.run("view.toggleConsole");
    MainWindow::settle();
}
