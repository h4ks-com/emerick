#include "emerick/app/console_completion.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include "emerick/app/lua_host.hpp"
#include "emerick/app/lua_text.hpp"

namespace emerick::app {

namespace {

constexpr std::size_t kMaxItems = 50;
constexpr double kDetailWeight = 0.8;
constexpr std::string_view kEmerickPrefix = "emerick.";

enum class Slot : std::uint8_t { None, Member, Global, Action, Setting, Help, Track };

struct Opening {
    std::string_view call;
    Slot slot;
};

constexpr std::array kStringSlots{Opening{"emerick.run(", Slot::Action}, Opening{"emerick.setting(", Slot::Setting},
                                  Opening{"help(", Slot::Help}, Opening{"emerick.ui.invoke(", Slot::Action},
                                  Opening{"emerick.ui.reveal(", Slot::Help}};

struct Global {
    std::string_view name;
    std::string_view detail;
};

constexpr std::array kGlobals{Global{"emerick", "the Emerick API: run, actions, setting, state, project, ui"},
                              Global{"help", "help(name?) describes an action, a setting or a function"},
                              Global{"print", "print(...) writes to the console"}};

struct Context {
    Slot slot = Slot::None;
    std::size_t from = 0;
    std::string_view query;
    char quote = 0;
};

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string_view trimmedRight(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

// Track params read as track="…" or track = "…".
bool namesTrack(std::string_view before) {
    if (!before.ends_with('=')) {
        return false;
    }
    const std::string_view key = trimmedRight(before.substr(0, before.size() - 1));
    constexpr std::string_view kTrack = "track";
    return key.ends_with(kTrack) && (key.size() == kTrack.size() || !isWordChar(key[key.size() - kTrack.size() - 1]));
}

Context stringContext(std::string_view line, std::size_t open, char quote) {
    const std::string_view before = trimmedRight(line.substr(0, open));
    const std::string_view query = line.substr(open + 1);
    for (const Opening& opening : kStringSlots) {
        if (before.ends_with(opening.call)) {
            return {opening.slot, open + 1, query, quote};
        }
    }
    return namesTrack(before) ? Context{Slot::Track, open + 1, query, quote} : Context{};
}

Context contextAt(std::string_view line) {
    std::optional<std::size_t> open;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (open) {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                open.reset();
            }
        } else if (c == '"' || c == '\'') {
            open = i;
            quote = c;
        } else if (line.substr(i).starts_with("--")) {
            return {};
        }
    }
    if (open) {
        return stringContext(line, *open, quote);
    }
    std::size_t start = line.size();
    while (start > 0 && (isWordChar(line[start - 1]) || line[start - 1] == '.')) {
        --start;
    }
    const std::string_view chain = line.substr(start);
    if (chain.starts_with(kEmerickPrefix)) {
        return {Slot::Member, start + kEmerickPrefix.size(), chain.substr(kEmerickPrefix.size())};
    }
    // We wait for a second letter so a one letter variable does not open the list.
    const bool word = chain.size() > 1 && !chain.contains('.') && (chain.front() < '0' || chain.front() > '9');
    return word ? Context{Slot::Global, start, chain} : Context{};
}

class Candidates {
public:
    explicit Candidates(std::string_view query) : query_(query) {}

    void add(const std::string& label, std::string insert, std::string detail, std::string_view alsoMatch = {}) {
        const std::array fields{core::FuzzyField{label, 1.0}, core::FuzzyField{alsoMatch, kDetailWeight}};
        if (auto match = core::fuzzyMatchFields(query_, fields)) {
            items_.push_back({label, std::move(insert), std::move(detail), std::move(match->ranges), match->score});
        }
    }

    std::vector<Completion> ranked() && {
        std::ranges::sort(items_, [](const Completion& a, const Completion& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return a.label.size() != b.label.size() ? a.label.size() < b.label.size() : a.label < b.label;
        });
        items_.resize(std::min(items_.size(), kMaxItems));
        return std::move(items_);
    }

private:
    std::string_view query_;
    std::vector<Completion> items_;
};

void addActions(Candidates& candidates, const Registry& registry, const std::string& closing) {
    for (const Action& entry : registry.actions()) {
        candidates.add(entry.id, entry.id + closing, entry.title + "   (" + paramSummary(entry.schema) + ")",
                       entry.title);
    }
}

void addSettings(Candidates& candidates, const Registry& registry, const std::string& closing) {
    for (const Setting& entry : registry.allSettings()) {
        candidates.add(entry.id, entry.id + closing, entry.title + "   (" + enumKey(entry.type) + ")", entry.title);
    }
}

void addTracks(Candidates& candidates, const Registry& registry, const std::string& closing) {
    Json tracks;
    try {
        tracks = registry.state("project").at("tracks");
    } catch (const AppError&) {
        return;
    }
    for (const Json& track : tracks) {
        const auto name = track.at("name").get<std::string>();
        const auto id = track.at("id").get<std::string>();
        candidates.add(name, name + closing, "track " + id);
        candidates.add(id, id + closing, "id of " + name);
    }
}

}  // namespace

Completions complete(const Registry& registry, std::string_view text, std::size_t cursor) {
    cursor = std::min(cursor, text.size());
    const std::size_t lineStart = text.substr(0, cursor).rfind('\n') + 1;
    const Context context = contextAt(text.substr(lineStart, cursor - lineStart));
    const bool closed = cursor < text.size() && text[cursor] == context.quote;
    const std::string closing = context.quote == 0 || closed ? "" : std::string(1, context.quote);
    Candidates candidates(context.query);
    switch (context.slot) {
        case Slot::None:
            return {cursor, {}};
        case Slot::Member:
            for (const LuaMember& member : luaMembers()) {
                candidates.add(std::string{member.name}, std::string{member.name},
                               std::string{member.signature} + "   " + std::string{member.summary});
            }
            break;
        case Slot::Global:
            for (const Global& global : kGlobals) {
                candidates.add(std::string{global.name}, std::string{global.name}, std::string{global.detail});
            }
            break;
        case Slot::Action:
            addActions(candidates, registry, closing);
            break;
        case Slot::Setting:
            addSettings(candidates, registry, closing);
            break;
        case Slot::Help:
            addActions(candidates, registry, closing);
            addSettings(candidates, registry, closing);
            break;
        case Slot::Track:
            addTracks(candidates, registry, closing);
            break;
    }
    return {lineStart + context.from, std::move(candidates).ranked()};
}

}  // namespace emerick::app
