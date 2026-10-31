#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace emerick::core {

// Byte offsets into the matched text, half open.
struct MatchRange {
    std::size_t begin = 0;
    std::size_t end = 0;
    auto operator<=>(const MatchRange&) const = default;
};

struct FuzzyMatch {
    double score = 0;
    std::vector<MatchRange> ranges;
};

struct FuzzyField {
    std::string_view text;
    double weight = 1.0;
};

// ASCII lowercase, which leaves every other byte of UTF-8 text as it is.
char toLower(char c);
std::string toLower(std::string_view text);

// Every space separated term must match (AND, any order). A term with an uppercase letter matches case sensitively.
std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view text);

// Each term scores in the field where it matches best, times that field's weight. Ranges are those in fields[0].
std::optional<FuzzyMatch> fuzzyMatchFields(std::string_view query, std::span<const FuzzyField> fields);

}  // namespace emerick::core
