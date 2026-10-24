#include "lane_blocks.hpp"

#include <QVariantMap>
#include <array>
#include <cstddef>
#include <vector>

#include "emerick/app/registry.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

constexpr core::Ticks kLastBlockTicks = 4 * core::kTicksPerQuarter;

constexpr std::array kNoteNames{"C",  "C#", "Db", "D",  "D#", "Eb", "E",  "F", "F#",
                                "Gb", "G",  "G#", "Ab", "A",  "A#", "Bb", "B"};

// The pickers list sections in the order a song plays them.
constexpr std::array kPickerOrder{Section::Intro,  Section::Main,  Section::Fill,
                                  Section::FillTo, Section::Break, Section::Ending};
static_assert(kPickerOrder.size() == kSections.size());

QVariantMap choice(const QString& id, const QString& name) { return {{"id", id}, {"name", name}}; }

QString pickerName(const SectionEvent& event) {
    switch (event.kind) {
        case Section::Fill:
            return "Fill in to the same Main";
        case Section::FillTo:
            return "Fill in to Main " +
                   QString{sectionInfo(event.kind).variations[static_cast<std::size_t>(event.variation)]};
        case Section::Main:
        case Section::Break:
        case Section::Intro:
        case Section::Ending:
            return app::qs(sectionName(event));
    }
    return {};
}

// Indexes into Theme.trackColors: Main A to D take the first four, then Fill, Break, Intro and Ending.
constexpr std::array kSectionColors{0, 4, 4, 5, 6, 7};
static_assert(kSectionColors.size() == kSections.size());

template <class Event, class Describe>
QVariantList blocks(const QVariant& data, Describe describe) {
    std::vector<Event> events;
    try {
        events = eventsOf<Event>(app::toJson(data));
    } catch (const app::AppError&) {
        return {};
    }
    QVariantList result;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const core::Ticks at = events[index].at;
        QVariantMap block = describe(events[index]);
        block.insert("at", QVariant::fromValue(at));
        block.insert("end",
                     QVariant::fromValue(index + 1 < events.size() ? events[index + 1].at : at + kLastBlockTicks));
        result.append(block);
    }
    return result;
}

}  // namespace

QVariantList LaneBlocks::chordRoots() {
    QVariantList result;
    for (const char* name : kNoteNames) {
        result.append(choice(name, name));
    }
    return result;
}

QVariantList LaneBlocks::chordBasses() {
    QVariantList result{choice({}, "No bass")};
    result.append(chordRoots());
    return result;
}

QVariantList LaneBlocks::chordQualities() {
    QVariantList result;
    for (const ChordType& type : chordTypes()) {
        const QString suffix = app::qs(std::string(type.name));
        result.append(choice(suffix, suffix.isEmpty() ? "maj" : suffix));
    }
    return result;
}

QVariantList LaneBlocks::sectionChoices() {
    QVariantList result;
    for (const Section kind : kPickerOrder) {
        for (int variation = 0; variation < variationCount(sectionInfo(kind)); ++variation) {
            const SectionEvent event{0, kind, variation};
            result.append(choice(app::qs(sectionName(event)), pickerName(event)));
        }
    }
    return result;
}

QVariantMap LaneBlocks::chordParts(const QString& name) {
    const auto chord = Chord::parse(name.toStdString());
    if (!chord) {
        return {{"root", QString{}}, {"quality", QString{}}, {"bass", QString{}}};
    }
    return {{"root", chord->isNoChord() ? QString{} : app::qs(chord->root.name)},
            {"quality", app::qs(std::string(chordType(chord->type).name))},
            {"bass", chord->bass ? app::qs(chord->bass->name) : QString{}}};
}

QVariantList LaneBlocks::chordBlocks(const QVariant& data) {
    return blocks<ChordEvent>(data, [](const ChordEvent& event) {
        return QVariantMap{{"label", app::qs(event.chord.name())}, {"gap", event.chord.isNoChord()}, {"colorIndex", 0}};
    });
}

QVariantList LaneBlocks::sectionBlocks(const QVariant& data) {
    return blocks<SectionEvent>(data, [](const SectionEvent& event) {
        const int base = kSectionColors.at(static_cast<std::size_t>(event.kind));
        return QVariantMap{{"label", app::qs(sectionName(event))},
                           {"gap", false},
                           {"colorIndex", event.kind == Section::Main ? base + event.variation : base}};
    });
}

}  // namespace emerick::yamaha
