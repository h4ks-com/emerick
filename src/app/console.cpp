#include "emerick/app/console.hpp"

#include <QColor>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>

#include "emerick/app/console_completion.hpp"
#include "emerick/app/lua_text.hpp"

namespace emerick::app {

namespace {

constexpr std::array kLineColors{"input", "print", "result", "error", "log"};
constexpr int kNoSerial = -1;

std::string shown(const Json& value) {
    if (value.is_null()) {
        return {};
    }
    return value.is_string() ? value.get<std::string>() : value.dump(2);
}

std::size_t index(ConsoleLine kind) { return static_cast<std::size_t>(kind); }

std::string sourceTag(const ConsoleEntry& line) {
    return line.kind == ConsoleLine::Log ? "  -- " + enumKey(line.source) : std::string{};
}

// Where the Lua starts in a line that shows code: after the prompt of an input line, or the whole action line.
std::optional<qsizetype> codeStart(ConsoleLine kind) {
    constexpr qsizetype kPromptLength = 2;
    switch (kind) {
        case ConsoleLine::Input:
            return kPromptLength;
        case ConsoleLine::Log:
            return 0;
        case ConsoleLine::Print:
        case ConsoleLine::Result:
        case ConsoleLine::Error:
            return std::nullopt;
    }
    return std::nullopt;
}

// QML counts text in UTF-16 units and the completer in UTF-8 bytes.
int utf16Length(const std::string& text, std::size_t bytes) {
    return static_cast<int>(QString::fromUtf8(text.data(), static_cast<qsizetype>(bytes)).size());
}

QVariantList utf16Ranges(const std::string& label, const std::vector<core::MatchRange>& ranges) {
    return ranges | std::views::transform([&](const core::MatchRange& range) {
               return QVariant{QVariantList{utf16Length(label, range.begin), utf16Length(label, range.end)}};
           }) |
           std::ranges::to<QVariantList>();
}

}  // namespace

Console::Console(Registry& registry, LuaHost& lua, QObject* parent) : QObject(parent), registry_(registry), lua_(lua) {
    connect(&registry_, &Registry::actionRan, this,
            [this](const std::string& id, const Json& params, ActionSource source) { logAction(id, params, source); });
}

Json Console::runChunk(const LuaChunk& chunk) {
    std::function<void(const std::string&)> outer;
    if (depth_ == 0) {
        outer = lua_.setPrintSink({});
        lua_.setPrintSink([this, outer](const std::string& line) {
            append(ConsoleLine::Print, line);
            if (outer) {
                outer(line);
            }
        });
    }
    ++depth_;
    Json result = lua_.eval(chunk);
    --depth_;
    if (depth_ == 0) {
        lua_.setPrintSink(std::move(outer));
    }
    return result;
}

Json Console::eval(const std::string& code) {
    pushHistory(code);
    std::string prompt = "> ";
    for (const auto line : code | std::views::split('\n')) {
        append(ConsoleLine::Input, prompt + std::string{std::string_view{line}});
        prompt = ". ";
    }
    const Json result = runChunk({.code = code, .name = "=console", .expression = true});
    if (result.at("ok") == true) {
        const std::string text = shown(result.at("result"));
        if (!text.empty()) {
            append(ConsoleLine::Result, text);
        }
        return {{"ok", true}, {"result", result.at("result")}, {"text", text}};
    }
    const std::string error = errorOf(result);
    append(ConsoleLine::Error, error);
    return {{"ok", false}, {"error", error}, {"text", error}};
}

Json Console::runScript(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw AppError("cannot read script " + path.string());
    }
    const std::string code{std::istreambuf_iterator<char>(file), {}};
    const Json result = runChunk({.code = code, .name = "=" + path.filename().string()});
    if (result.at("ok") != true) {
        const std::string error = errorOf(result);
        // A script run from the console reports its error through the console's own line.
        if (depth_ == 0) {
            append(ConsoleLine::Error, error);
        }
        throw AppError(error);
    }
    return {{"result", result.at("result")}, {"output", result.at("output")}};
}

void Console::append(ConsoleLine kind, const std::string& text, ActionSource source) {
    for (const auto part : text | std::views::split('\n')) {
        entries_.push_back({nextSerial_++, kind, std::string{std::string_view{part}}, source});
        render(entries_.back());
    }
    while (entries_.size() > kConsoleLines) {
        const ConsoleEntry dropped = std::move(entries_.front());
        entries_.pop_front();
        if (document_ == nullptr || !shows(dropped.kind)) {
            continue;
        }
        if (document_->blockCount() == 1) {
            document_->clear();
            continue;
        }
        QTextCursor cursor(document_);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        // Removing the first line merges the second into its block, which keeps the first line's serial.
        const auto next = std::ranges::find_if(entries_, [this](const ConsoleEntry& line) { return shows(line.kind); });
        document_->firstBlock().setUserState(next == entries_.end() ? kNoSerial : next->serial);
    }
}

void Console::clear() {
    entries_.clear();
    rebuild();
}

void Console::setFilter(ConsoleFilter filter) {
    if (filter_ != filter) {
        filter_ = filter;
        rebuild();
        emit filterChanged();
    }
}

const ConsoleEntry& Console::entry(int serial) const {
    if (entries_.empty() || serial < entries_.front().serial || serial > entries_.back().serial) {
        throw AppError("console line " + std::to_string(serial) + " is no longer kept");
    }
    return entries_[static_cast<std::size_t>(serial - entries_.front().serial)];
}

std::string Console::shownText() const {
    std::string text;
    for (const ConsoleEntry& line :
         entries_ | std::views::filter([this](const ConsoleEntry& entry) { return shows(entry.kind); })) {
        text += (text.empty() ? "" : "\n") + line.text + sourceTag(line);
    }
    return text;
}

Json Console::recent(std::size_t count) const {
    const std::size_t skipped = entries_.size() - std::min(count, entries_.size());
    return entries_ | std::views::drop(skipped) | std::views::transform([](const ConsoleEntry& line) {
               return Json{{"kind", enumKey(line.kind)}, {"text", line.text}, {"source", enumKey(line.source)}};
           }) |
           std::ranges::to<Json::array_t>();
}

std::vector<std::string> Console::history() const {
    return registry_.settingValue(kConsoleHistorySetting).get<std::vector<std::string>>();
}

void Console::pushHistory(const std::string& code) {
    historyCursor_ = -1;
    if (code.find_first_not_of(" \t\n") == std::string::npos) {
        return;
    }
    std::vector<std::string> entries = history();
    if (!entries.empty() && entries.back() == code) {
        return;
    }
    entries.push_back(code);
    if (entries.size() > kConsoleHistory) {
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(kConsoleHistory));
    }
    registry_.setSetting(kConsoleHistorySetting, entries);
}

QString Console::historyStep(int step, const QString& draft) {
    const std::vector<std::string> entries = history();
    const int newest = static_cast<int>(entries.size());
    if (historyCursor_ < 0) {
        if (step > 0 || entries.empty()) {
            return draft;
        }
        historyDraft_ = draft;
        historyCursor_ = newest;
    }
    historyCursor_ = std::clamp(historyCursor_ + step, 0, newest);
    if (historyCursor_ == newest) {
        historyCursor_ = -1;
        return historyDraft_;
    }
    return qs(entries[static_cast<std::size_t>(historyCursor_)]);
}

QVariantMap Console::completions(const QString& text, int cursor) const {
    const std::string utf8 = text.toStdString();
    const auto bytes = static_cast<std::size_t>(text.left(cursor).toUtf8().size());
    const Completions found = complete(registry_, utf8, bytes);
    QVariantList items;
    for (const Completion& item : found.items) {
        items.append(QVariantMap{{"label", qs(item.label)},
                                 {"insert", qs(item.insert)},
                                 {"detail", qs(item.detail)},
                                 {"ranges", utf16Ranges(item.label, item.ranges)}});
    }
    return {{"from", utf16Length(utf8, found.from)}, {"to", cursor}, {"items", items}};
}

QVariantMap Console::entryAt(int position) const {
    if (document_ == nullptr) {
        return {};
    }
    const int serial = document_->findBlock(position).userState();
    if (entries_.empty() || serial < entries_.front().serial || serial > entries_.back().serial) {
        return {};
    }
    const ConsoleEntry& line = entry(serial);
    return {{"serial", line.serial}, {"kind", static_cast<int>(line.kind)}, {"text", qs(line.text)}};
}

void Console::attach(QTextDocument* document, const QVariantMap& colors) {
    document_ = document;
    for (std::size_t kind = 0; kind < formats_.size(); ++kind) {
        formats_.at(kind).setForeground(colors.value(kLineColors.at(kind)).value<QColor>());
    }
    sourceFormat_.setForeground(colors.value("source").value<QColor>());
    codeFormats_ = codeFormats(colors);
    rebuild();
}

void Console::attachView(QQuickTextDocument* view, const QVariantMap& colors) {
    attach(view == nullptr ? nullptr : view->textDocument(), colors);
}

void Console::logAction(const std::string& id, const Json& params, ActionSource source) {
    append(ConsoleLine::Log, luaCall(id, namedTracks(params)), source);
}

Json Console::namedTracks(const Json& params) const {
    if (!params.is_object() || !params.contains("track") || !params.at("track").is_string()) {
        return params;
    }
    Json tracks;
    try {
        tracks = registry_.state("project").at("tracks");
    } catch (const AppError&) {
        return params;
    }
    const auto id = params.at("track").get<std::string>();
    const auto found = std::ranges::find_if(tracks, [&](const Json& track) { return track.at("id") == id; });
    if (found == tracks.end() ||
        std::ranges::count_if(tracks, [&](const Json& track) { return track.at("name") == found->at("name"); }) != 1) {
        return params;
    }
    Json named = params;
    named["track"] = found->at("name");
    return named;
}

bool Console::shows(ConsoleLine kind) const {
    switch (filter_) {
        case ConsoleFilter::All:
            return true;
        case ConsoleFilter::Info:
            return kind == ConsoleLine::Log;
        case ConsoleFilter::Output:
            return kind == ConsoleLine::Input || kind == ConsoleLine::Print || kind == ConsoleLine::Result;
        case ConsoleFilter::Errors:
            return kind == ConsoleLine::Error;
    }
    return true;
}

void Console::render(const ConsoleEntry& line) {
    if (document_ == nullptr || !shows(line.kind)) {
        return;
    }
    QTextCursor cursor(document_);
    cursor.movePosition(QTextCursor::End);
    if (document_->blockCount() > 1 || document_->firstBlock().userState() != kNoSerial) {
        cursor.insertBlock();
    }
    const int lineStart = cursor.position();
    cursor.insertText(qs(line.text), formats_.at(index(line.kind)));
    if (const auto code = codeStart(line.kind)) {
        const QString text = qs(line.text).mid(*code);
        for (const QTextLayout::FormatRange& range : luaFormats(text, codeFormats_)) {
            cursor.setPosition(lineStart + static_cast<int>(*code) + range.start);
            cursor.setPosition(cursor.position() + range.length, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(range.format);
        }
        cursor.movePosition(QTextCursor::End);
    }
    cursor.insertText(qs(sourceTag(line)), sourceFormat_);
    cursor.block().setUserState(line.serial);
}

void Console::rebuild() {
    if (document_ == nullptr) {
        return;
    }
    document_->clear();
    for (const ConsoleEntry& line : entries_) {
        render(line);
    }
}

}  // namespace emerick::app
