#pragma once

#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QVariantMap>
#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "emerick/app/enums.hpp"

namespace emerick::app {

struct CodeSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    CodeToken token = CodeToken::Keyword;
    auto operator<=>(const CodeSpan&) const = default;
};

// The highlighted spans of Lua source as byte ranges, in source order, from tree-sitter's parse.
std::vector<CodeSpan> luaSpans(std::string_view source);

using CodeFormats = std::array<QTextCharFormat, kCodeTokens>;

// Each token's format from a map of token keys, such as "keyword" and "actionId", to colors.
CodeFormats codeFormats(const QVariantMap& colors);
// The formats of one line of Lua, in UTF-16 positions as QTextLayout counts them.
QList<QTextLayout::FormatRange> luaFormats(const QString& line, const CodeFormats& formats);

// Highlights Lua as the user types; QML attaches it to a TextEdit's textDocument.
class CodeHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap colors READ colors WRITE setColors NOTIFY colorsChanged)

public:
    explicit CodeHighlighter(QObject* parent = nullptr);

    [[nodiscard]] QQuickTextDocument* view() const { return view_; }
    void setView(QQuickTextDocument* view);
    [[nodiscard]] QVariantMap colors() const { return colors_; }
    void setColors(const QVariantMap& colors);

signals:
    void viewChanged();
    void colorsChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    QPointer<QQuickTextDocument> view_;
    QVariantMap colors_;
    CodeFormats formats_;
};

}  // namespace emerick::app
