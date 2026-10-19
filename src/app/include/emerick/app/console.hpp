#pragma once

#include <QObject>
#include <QPointer>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVariantMap>
#include <array>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include "emerick/app/code_highlighter.hpp"
#include "emerick/app/enums.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/registry.hpp"

class QQuickTextDocument;

namespace emerick::app {

inline constexpr std::size_t kConsoleLines = 5000;
inline constexpr std::size_t kConsoleHistory = 500;
inline constexpr const char* kConsoleHistorySetting = "console.history";

struct ConsoleEntry {
    int serial = 0;
    ConsoleLine kind = ConsoleLine::Print;
    std::string text;
    ActionSource source = ActionSource::Code;
};

// The Lua console: one stream of REPL lines and the action log, capped at kConsoleLines lines, on the UI thread.
class Console : public QObject {
    Q_OBJECT
    Q_PROPERTY(emerick::app::ConsoleFilter filter READ filter NOTIFY filterChanged)

public:
    Console(Registry& registry, LuaHost& lua, QObject* parent = nullptr);

    // Echoes the code, runs it and appends what it printed and its value or error. Returns {ok, result | error,
    // text}, where text is the value or error as the console shows it.
    Json eval(const std::string& code);
    // Runs a script file with its output in the console; throws AppError when the script fails.
    Json runScript(const std::filesystem::path& path);
    void append(ConsoleLine kind, const std::string& text, ActionSource source = ActionSource::Code);
    void clear();
    void setFilter(ConsoleFilter filter);

    [[nodiscard]] ConsoleFilter filter() const { return filter_; }
    [[nodiscard]] const std::deque<ConsoleEntry>& entries() const { return entries_; }
    [[nodiscard]] const ConsoleEntry& entry(int serial) const;
    // The last count lines as {kind, text, source}.
    [[nodiscard]] Json recent(std::size_t count) const;
    [[nodiscard]] std::vector<std::string> history() const;
    // Mirrors the lines the filter shows into document, in each kind's color.
    void attach(QTextDocument* document, const QVariantMap& colors);

    Q_INVOKABLE void attachView(QQuickTextDocument* view, const QVariantMap& colors);
    // Steps through history from the newest entry: -1 goes back, +1 forward. The draft returns past the newest.
    Q_INVOKABLE QString historyStep(int step, const QString& draft);
    // {from, to, items: [{label, insert, detail, ranges}]} for the text before cursor.
    Q_INVOKABLE QVariantMap completions(const QString& text, int cursor) const;
    // The line at a text position of the attached document as {serial, kind, text}.
    Q_INVOKABLE QVariantMap entryAt(int position) const;

signals:
    void filterChanged();

private:
    void pushHistory(const std::string& code);
    void logAction(const std::string& id, const Json& params, ActionSource source);
    [[nodiscard]] Json namedTracks(const Json& params) const;
    [[nodiscard]] bool shows(ConsoleLine kind) const;
    void render(const ConsoleEntry& line);
    void rebuild();
    Json runChunk(const LuaChunk& chunk);

    Registry& registry_;
    LuaHost& lua_;
    std::deque<ConsoleEntry> entries_;
    int nextSerial_ = 1;
    int depth_ = 0;
    ConsoleFilter filter_ = ConsoleFilter::All;
    int historyCursor_ = -1;
    QString historyDraft_;
    QPointer<QTextDocument> document_;
    std::array<QTextCharFormat, 5> formats_;
    QTextCharFormat sourceFormat_;
    CodeFormats codeFormats_;
};

// view.toggleConsole, the console.* actions, the console location and the console.history setting.
void registerConsoleActions(Registry& registry, Nav& nav, Console& console);

}  // namespace emerick::app
