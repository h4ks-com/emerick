#include "emerick/app/code_highlighter.hpp"

#include <QColor>
#include <QTextBlock>
#include <QTextDocument>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

#include "qt_app.hpp"

using namespace emerick::app;

namespace {

std::vector<std::pair<std::string, CodeToken>> tokens(std::string_view source) {
    std::vector<std::pair<std::string, CodeToken>> found;
    for (const CodeSpan& span : luaSpans(source)) {
        found.emplace_back(std::string{source.substr(span.begin, span.end - span.begin)}, span.token);
    }
    return found;
}

}  // namespace

TEST_CASE("a Lua line splits into keywords, strings, numbers, comments, calls, API names and action ids") {
    using enum CodeToken;
    CHECK(
        tokens(
            R"(local n = 2 if emerick.run("track.setMute", {track = "Keys", value = true}) then print(n) end -- done)") ==
        std::vector<std::pair<std::string, CodeToken>>{{"local", Keyword},
                                                       {"2", Number},
                                                       {"if", Keyword},
                                                       {"emerick.run", Api},
                                                       {"\"track.setMute\"", ActionId},
                                                       {"\"Keys\"", String},
                                                       {"true", Keyword},
                                                       {"then", Keyword},
                                                       {"print", Call},
                                                       {"end", Keyword},
                                                       {"-- done", Comment}});
    CHECK(tokens("t:add(1.5)") == std::vector<std::pair<std::string, CodeToken>>{{"add", Call}, {"1.5", Number}});
    CHECK(tokens("x = emerick.ui.find") == std::vector<std::pair<std::string, CodeToken>>{{"emerick.ui.find", Api}});
    CHECK(tokens("emerick.run(\"unfinished").size() >= 1);
}

TEST_CASE("the highlighter colors a document line in UTF-16 positions") {
    ensureApplication();
    QTextDocument document(QString::fromUtf8("print(\"é\") -- ok"));
    CodeHighlighter highlighter;
    highlighter.setDocument(&document);
    highlighter.setColors({{"call", QColor(Qt::red)}, {"string", QColor(Qt::green)}, {"comment", QColor(Qt::blue)}});
    const QList<QTextLayout::FormatRange> ranges = document.firstBlock().layout()->formats();
    REQUIRE(ranges.size() == 3);
    CHECK((ranges[0].start == 0 && ranges[0].length == 5 && ranges[0].format.foreground().color() == Qt::red));
    CHECK((ranges[1].start == 6 && ranges[1].length == 3 && ranges[1].format.foreground().color() == Qt::green));
    CHECK((ranges[2].start == 11 && ranges[2].length == 5 && ranges[2].format.foreground().color() == Qt::blue));
}
