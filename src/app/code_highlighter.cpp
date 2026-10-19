#include "emerick/app/code_highlighter.hpp"

#include <tree_sitter/api.h>

#include <QColor>
#include <QQuickTextDocument>
#include <algorithm>
#include <cstdint>
#include <memory>

extern "C" const TSLanguage* tree_sitter_lua();

namespace emerick::app {

namespace {

enum class NodeRule : std::uint8_t { Token, Index, Call };

struct NodeKind {
    std::string_view type;
    NodeRule rule;
    CodeToken token;
};

constexpr std::array kNodeKinds{
    NodeKind{"comment", NodeRule::Token, CodeToken::Comment},
    NodeKind{"string", NodeRule::Token, CodeToken::String},
    NodeKind{"number", NodeRule::Token, CodeToken::Number},
    NodeKind{"true", NodeRule::Token, CodeToken::Keyword},
    NodeKind{"false", NodeRule::Token, CodeToken::Keyword},
    NodeKind{"nil", NodeRule::Token, CodeToken::Keyword},
    NodeKind{"dot_index_expression", NodeRule::Index, CodeToken::Api},
    NodeKind{"method_index_expression", NodeRule::Index, CodeToken::Api},
    NodeKind{"function_call", NodeRule::Call, CodeToken::Call},
};

constexpr std::array<std::string_view, 19> kKeywords{"and",      "break",  "do",   "else",  "elseif", "end", "for",
                                                     "function", "goto",   "if",   "in",    "local",  "not", "or",
                                                     "repeat",   "return", "then", "until", "while"};

// The calls whose first argument is an action or setting id.
constexpr std::array<std::string_view, 5> kIdCalls{"emerick.run", "emerick.setting", "help", "emerick.ui.invoke",
                                                   "emerick.ui.reveal"};

constexpr std::string_view kApiTable = "emerick";

TSNode field(TSNode node, std::string_view name) {
    return ts_node_child_by_field_name(node, name.data(), static_cast<std::uint32_t>(name.size()));
}

class SpanWalker {
public:
    explicit SpanWalker(std::string_view source) : source_(source) {}

    void walk(TSNode node) {
        const std::string_view type = ts_node_type(node);
        if (!ts_node_is_named(node)) {
            if (std::ranges::contains(kKeywords, type)) {
                add(node, CodeToken::Keyword);
            }
            return;
        }
        const auto* kind = std::ranges::find(kNodeKinds, type, &NodeKind::type);
        if (kind == kNodeKinds.end()) {
            walkChildren(node);
            return;
        }
        switch (kind->rule) {
            case NodeRule::Token:
                add(node, kind->token);
                return;
            case NodeRule::Index:
                if (isApi(node)) {
                    add(node, CodeToken::Api);
                } else {
                    walkChildren(node);
                }
                return;
            case NodeRule::Call:
                walkCall(node);
                return;
        }
    }

    std::vector<CodeSpan> spans() && {
        std::ranges::sort(spans_);
        return std::move(spans_);
    }

private:
    [[nodiscard]] std::string_view text(TSNode node) const {
        return source_.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
    }

    void add(TSNode node, CodeToken token) {
        spans_.push_back({ts_node_start_byte(node), ts_node_end_byte(node), token});
    }

    void walkChildren(TSNode node) {
        for (std::uint32_t index = 0; index < ts_node_child_count(node); ++index) {
            walk(ts_node_child(node, index));
        }
    }

    // A name such as emerick.ui.find, whose leftmost table is emerick.
    [[nodiscard]] bool isApi(TSNode node) const {
        while (!ts_node_is_null(node) && ts_node_named_child_count(node) > 0 &&
               std::string_view{ts_node_type(node)} != "identifier") {
            node = field(node, "table");
        }
        return !ts_node_is_null(node) && text(node) == kApiTable;
    }

    void walkName(TSNode name) {
        if (isApi(name)) {
            add(name, CodeToken::Api);
            return;
        }
        if (std::string_view{ts_node_type(name)} == "identifier") {
            add(name, CodeToken::Call);
            return;
        }
        const TSNode method = field(name, "method");
        const TSNode member = ts_node_is_null(method) ? field(name, "field") : method;
        if (ts_node_is_null(member)) {
            walk(name);
            return;
        }
        walk(field(name, "table"));
        add(member, CodeToken::Call);
    }

    void walkCall(TSNode call) {
        const TSNode name = field(call, "name");
        const TSNode arguments = field(call, "arguments");
        walkName(name);
        if (ts_node_is_null(arguments)) {
            return;
        }
        const bool takesId = std::ranges::contains(kIdCalls, text(name));
        const TSNode first = ts_node_named_child(arguments, 0);
        for (std::uint32_t index = 0; index < ts_node_child_count(arguments); ++index) {
            const TSNode argument = ts_node_child(arguments, index);
            if (takesId && ts_node_eq(argument, first) && std::string_view{ts_node_type(argument)} == "string") {
                add(argument, CodeToken::ActionId);
            } else {
                walk(argument);
            }
        }
    }

    std::string_view source_;
    std::vector<CodeSpan> spans_;
};

std::vector<int> utf16Offsets(const QByteArray& utf8) {
    constexpr unsigned char kContinuationMask = 0xC0;
    constexpr unsigned char kContinuation = 0x80;
    constexpr unsigned char kFourByteLead = 0xF0;
    std::vector<int> offsets(static_cast<std::size_t>(utf8.size()) + 1);
    int units = 0;
    for (qsizetype index = 0; index < utf8.size(); ++index) {
        offsets[static_cast<std::size_t>(index)] = units;
        const auto byte = static_cast<unsigned char>(utf8[index]);
        if ((byte & kContinuationMask) != kContinuation) {
            units += byte >= kFourByteLead ? 2 : 1;
        }
    }
    offsets.back() = units;
    return offsets;
}

}  // namespace

std::vector<CodeSpan> luaSpans(std::string_view source) {
    const std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);
    ts_parser_set_language(parser.get(), tree_sitter_lua());
    const std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<std::uint32_t>(source.size())),
        ts_tree_delete);
    if (tree == nullptr) {
        return {};
    }
    SpanWalker walker(source);
    walker.walk(ts_tree_root_node(tree.get()));
    return std::move(walker).spans();
}

CodeFormats codeFormats(const QVariantMap& colors) {
    CodeFormats formats;
    for (std::size_t token = 0; token < kCodeTokens; ++token) {
        const auto color = colors.value(QString::fromStdString(enumKey(static_cast<CodeToken>(token)))).value<QColor>();
        if (color.isValid()) {
            formats.at(token).setForeground(color);
        }
    }
    return formats;
}

QList<QTextLayout::FormatRange> luaFormats(const QString& line, const CodeFormats& formats) {
    const QByteArray utf8 = line.toUtf8();
    const std::vector<int> offsets = utf16Offsets(utf8);
    QList<QTextLayout::FormatRange> ranges;
    for (const CodeSpan& span : luaSpans({utf8.constData(), static_cast<std::size_t>(utf8.size())})) {
        const int start = offsets.at(span.begin);
        ranges.append({start, offsets.at(span.end) - start, formats.at(static_cast<std::size_t>(span.token))});
    }
    return ranges;
}

CodeHighlighter::CodeHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {}

void CodeHighlighter::setView(QQuickTextDocument* view) {
    if (view_ == view) {
        return;
    }
    view_ = view;
    setDocument(view == nullptr ? nullptr : view->textDocument());
    emit viewChanged();
}

void CodeHighlighter::setColors(const QVariantMap& colors) {
    colors_ = colors;
    formats_ = codeFormats(colors);
    rehighlight();
    emit colorsChanged();
}

void CodeHighlighter::highlightBlock(const QString& text) {
    for (const QTextLayout::FormatRange& range : luaFormats(text, formats_)) {
        setFormat(range.start, range.length, range.format);
    }
}

}  // namespace emerick::app
