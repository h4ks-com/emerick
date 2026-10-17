#include "emerick/core/fuzzy.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string>

namespace emerick::core {

namespace {

// Scores follow the shape of fzf's v2 algorithm (junegunn/fzf, src/algo/algo.go).
constexpr int kMatch = 16;
constexpr int kGapStart = -3;
constexpr int kGapExtend = -1;
constexpr int kBoundaryWhite = 10;
constexpr int kBoundaryDelimiter = 9;
constexpr int kBoundary = 8;
constexpr int kCamel = 7;
constexpr int kConsecutive = 4;
constexpr int kFirstCharMultiplier = 2;
constexpr int kNone = std::numeric_limits<int>::min() / 4;
constexpr std::size_t kNoCell = std::numeric_limits<std::size_t>::max();

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) { return isLower(c) || isUpper(c) || isDigit(c); }
char toLower(char c) { return isUpper(c) ? static_cast<char>(c - 'A' + 'a') : c; }

int bonusAt(std::string_view text, std::size_t j) {
    if (j == 0) {
        return kBoundaryWhite;
    }
    const char previous = text[j - 1];
    const char current = text[j];
    if (previous == ' ') {
        return kBoundaryWhite;
    }
    if (std::string_view{"./_-:,"}.contains(previous)) {
        return kBoundaryDelimiter;
    }
    if (!isAlnum(previous) && isAlnum(current)) {
        return kBoundary;
    }
    if ((isLower(previous) && isUpper(current)) || (!isDigit(previous) && isDigit(current))) {
        return kCamel;
    }
    return 0;
}

struct TermMatch {
    int score = 0;
    std::vector<std::size_t> positions;
};

std::optional<TermMatch> matchTerm(std::string_view term, std::string_view text) {
    const std::size_t n = term.size();
    const std::size_t m = text.size();
    if (n == 0 || n > m) {
        return std::nullopt;
    }
    const bool caseSensitive = std::ranges::any_of(term, isUpper);
    const auto same = [caseSensitive](char a, char b) { return caseSensitive ? a == b : toLower(a) == toLower(b); };

    std::vector<int> score(n * m, kNone);
    std::vector<int> runBonus(n * m, 0);
    std::vector<std::size_t> from(n * m, kNoCell);
    for (std::size_t i = 0; i < n; ++i) {
        int gapBest = kNone;
        std::size_t gapFrom = kNoCell;
        for (std::size_t j = i; j < m; ++j) {
            if (i > 0 && j >= 2) {
                if (gapBest != kNone) {
                    gapBest += kGapExtend;
                }
                const std::size_t skipped = ((i - 1) * m) + j - 2;
                if (score[skipped] != kNone && score[skipped] + kGapStart > gapBest) {
                    gapBest = score[skipped] + kGapStart;
                    gapFrom = skipped;
                }
            }
            if (!same(term[i], text[j])) {
                continue;
            }
            const std::size_t cell = (i * m) + j;
            const int bonus = bonusAt(text, j);
            if (i == 0) {
                score[cell] = kMatch + (bonus * kFirstCharMultiplier);
                runBonus[cell] = bonus;
                continue;
            }
            const std::size_t diagonal = ((i - 1) * m) + j - 1;
            if (score[diagonal] != kNone) {
                const int carried = std::max({bonus, runBonus[diagonal], kConsecutive});
                score[cell] = score[diagonal] + kMatch + carried;
                runBonus[cell] = carried;
                from[cell] = diagonal;
            }
            if (gapBest != kNone && gapBest + kMatch + bonus > score[cell]) {
                score[cell] = gapBest + kMatch + bonus;
                runBonus[cell] = bonus;
                from[cell] = gapFrom;
            }
        }
    }

    const auto lastRow = std::span{score}.subspan((n - 1) * m, m);
    const auto best = std::ranges::max_element(lastRow);
    if (*best == kNone) {
        return std::nullopt;
    }
    TermMatch match{*best, {}};
    for (std::size_t cell = ((n - 1) * m) + static_cast<std::size_t>(best - lastRow.begin()); cell != kNoCell;
         cell = from[cell]) {
        match.positions.push_back(cell % m);
    }
    std::ranges::reverse(match.positions);
    return match;
}

std::vector<MatchRange> toRanges(std::vector<std::size_t> positions) {
    std::ranges::sort(positions);
    std::vector<MatchRange> ranges;
    for (const std::size_t position : positions) {
        if (!ranges.empty() && ranges.back().end >= position) {
            ranges.back().end = std::max(ranges.back().end, position + 1);
        } else {
            ranges.push_back({position, position + 1});
        }
    }
    return ranges;
}

auto terms(std::string_view query) {
    return query | std::views::split(' ') | std::views::transform([](auto part) { return std::string_view{part}; }) |
           std::views::filter([](std::string_view part) { return !part.empty(); });
}

}  // namespace

std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view text) {
    const std::array fields{FuzzyField{text, 1.0}};
    return fuzzyMatchFields(query, fields);
}

std::optional<FuzzyMatch> fuzzyMatchFields(std::string_view query, std::span<const FuzzyField> fields) {
    FuzzyMatch result;
    std::vector<std::size_t> titlePositions;
    for (const std::string_view term : terms(query)) {
        double bestScore = 0;
        std::optional<std::size_t> bestField;
        std::vector<std::size_t> bestPositions;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            auto match = matchTerm(term, fields[f].text);
            if (match && (!bestField || match->score * fields[f].weight > bestScore)) {
                bestScore = match->score * fields[f].weight;
                bestField = f;
                bestPositions = std::move(match->positions);
            }
        }
        if (!bestField) {
            return std::nullopt;
        }
        result.score += bestScore;
        if (*bestField == 0) {
            titlePositions.insert(titlePositions.end(), bestPositions.begin(), bestPositions.end());
        }
    }
    result.ranges = toRanges(std::move(titlePositions));
    return result;
}

}  // namespace emerick::core
