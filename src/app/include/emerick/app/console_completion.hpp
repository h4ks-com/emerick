#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/core/fuzzy.hpp"

namespace emerick::app {

struct Completion {
    std::string label;
    std::string insert;
    std::string detail;
    // The matched bytes of label.
    std::vector<core::MatchRange> ranges;
    double score = 0;
};

// Accepting an item replaces the bytes from `from` to the cursor with its insert text.
struct Completions {
    std::size_t from = 0;
    std::vector<Completion> items;
};

// What fits at cursor (a byte offset into text): emerick members, action ids inside emerick.run("…"), setting ids
// inside emerick.setting("…"), both inside help("…"), and track names and ids inside track="…", ranked by the fuzzy
// matcher.
Completions complete(const Registry& registry, std::string_view text, std::size_t cursor);

}  // namespace emerick::app
