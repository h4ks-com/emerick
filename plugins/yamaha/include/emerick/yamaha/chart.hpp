#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "emerick/core/time.hpp"
#include "emerick/yamaha/chord.hpp"
#include "emerick/yamaha/raw_event.hpp"
#include "emerick/yamaha/sysex.hpp"

namespace emerick::yamaha {

struct ChordEvent {
    core::Ticks at = 0;
    Chord chord;
    bool operator==(const ChordEvent&) const = default;
};

// Fill is a fill that returns to the same Main, FillTo a fill that lands on Main `variation`. A Main's pattern
// repeats every `bars` bars from its start; 0 means we do not know, and we treat it as one bar.
struct SectionEvent {
    core::Ticks at = 0;
    Section kind = Section::Main;
    int variation = 0;
    int bars = 0;
    bool operator==(const SectionEvent&) const = default;
};

struct Chart {
    std::vector<ChordEvent> chords;
    std::vector<SectionEvent> sections;
};

struct ChartOptions {
    int shiftBars = 0;
    bool snapToBeat = true;
    bool anticipation = true;
};

void to_json(nlohmann::json& json, const ChordEvent& event);
void from_json(const nlohmann::json& json, ChordEvent& event);
void to_json(nlohmann::json& json, const SectionEvent& event);
void from_json(const nlohmann::json& json, SectionEvent& event);

// Events are the take's chord and section sysex, timed in core ticks.
Chart chartFromTake(const std::vector<RawEvent>& events, const core::TempoMap& tempo, const ChartOptions& options);

std::string sectionName(const SectionEvent& event);
// One "bar.beat  label" line per event, in time order.
std::string chartText(const Chart& chart, const core::TempoMap& tempo);

}  // namespace emerick::yamaha
