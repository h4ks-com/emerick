#include "lane_blocks.hpp"

#include <QVariantMap>
#include <array>
#include <cstddef>
#include <vector>

#include "emerick/app/registry.hpp"
#include "emerick/yamaha/lane_layout.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

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
    return app::toVariant(yamaha::chordBlocks(app::toJson(data))).toList();
}

QVariantList LaneBlocks::sectionBlocks(const QVariant& data) {
    return app::toVariant(yamaha::sectionBlocks(app::toJson(data))).toList();
}

}  // namespace emerick::yamaha
