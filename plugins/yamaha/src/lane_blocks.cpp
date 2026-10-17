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
