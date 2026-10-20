#include <QClipboard>
#include <QGuiApplication>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QtSystemDetection>
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
    return *std::ranges::find(console.entries() | std::views::reverse, ConsoleLine::Log, &ConsoleEntry::kind);
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
    auto* shown = main.item("console.output")->property("textDocument").value<QQuickTextDocument*>();
    REQUIRE(shown != nullptr);
    QTextCursor cursor(shown->textDocument()->firstBlock());
    cursor.setPosition(cursor.position() + 3);
    CHECK(cursor.charFormat().foreground().color() == QColor("#6FA0E0"));

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

TEST_CASE("the console log selects all and copies with the standard keys once clicked") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("view.toggleConsole");
    main.console.eval("print('first')");
    main.console.eval("print('second')");
    QQuickItem* output = main.item("console.output");
    main.click(output);
    REQUIRE(processUntil([&] { return output->hasActiveFocus(); }));

    QTest::keyClick(main.window, Qt::Key_A, Qt::ControlModifier);
    CHECK(output->property("selectedText").toString().contains("first"));
    QTest::keyClick(main.window, Qt::Key_C, Qt::ControlModifier);
    CHECK(QGuiApplication::clipboard()->text() == output->property("selectedText").toString());

    QGuiApplication::clipboard()->clear();
    main.click(main.item("console.copyAll"));
    CHECK(QGuiApplication::clipboard()->text() == QString::fromStdString(main.console.shownText()));
    main.rig.run("console.clear");
    main.rig.run("view.toggleConsole");
    MainWindow::settle();
}

TEST_CASE("the console prompt takes readline keys on the physical Control key and keeps Cmd for editing") {
    MainWindow& main = MainWindow::instance();
    main.rig.run("view.toggleConsole");
    QQuickItem* input = main.item("console.eval");
    REQUIRE(processUntil([&] { return input->hasActiveFocus(); }));
    const auto cursor = [&] { return input->property("cursorPosition").toInt(); };
    const auto text = [&] { return input->property("text").toString(); };
#ifdef Q_OS_MACOS
    constexpr Qt::KeyboardModifier kReadline = Qt::MetaModifier;
#else
    constexpr Qt::KeyboardModifier kReadline = Qt::ControlModifier;
#endif

    type(main.window, "return 1");
    QTest::keyClick(main.window, Qt::Key_Return);
    type(main.window, "foo bar");
    QTest::keyClick(main.window, Qt::Key_A, kReadline);
    CHECK(cursor() == 0);
    QTest::keyClick(main.window, Qt::Key_F, kReadline);
    CHECK(cursor() == 1);
    QTest::keyClick(main.window, Qt::Key_E, kReadline);
    CHECK(cursor() == 7);
    QTest::keyClick(main.window, Qt::Key_B, kReadline);
    CHECK(cursor() == 6);
    QTest::keyClick(main.window, Qt::Key_K, kReadline);
    CHECK(text() == "foo ba");
    QTest::keyClick(main.window, Qt::Key_W, kReadline);
    CHECK(text() == "foo ");
    QTest::keyClick(main.window, Qt::Key_U, kReadline);
    CHECK(text().isEmpty());
    QTest::keyClick(main.window, Qt::Key_P, kReadline);
    CHECK(text() == "return 1");
    QTest::keyClick(main.window, Qt::Key_N, kReadline);
    CHECK(text().isEmpty());

#ifdef Q_OS_MACOS
    type(main.window, "abc");
    QTest::keyClick(main.window, Qt::Key_A, Qt::ControlModifier);
    CHECK(input->property("selectedText") == "abc");
    QTest::keyClick(main.window, Qt::Key_C, Qt::ControlModifier);
    CHECK(QGuiApplication::clipboard()->text() == "abc");
    QTest::keyClick(main.window, Qt::Key_X, Qt::ControlModifier);
    CHECK(text().isEmpty());
    QTest::keyClick(main.window, Qt::Key_V, Qt::ControlModifier);
    CHECK(text() == "abc");
#endif

    QTest::keyClick(main.window, Qt::Key_L, kReadline);
    CHECK(main.console.entries().empty());
    main.rig.run("view.toggleConsole");
    MainWindow::settle();
}
