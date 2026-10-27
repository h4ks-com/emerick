#include "arranger_styles.hpp"

#include <cmath>
#include <format>
#include <system_error>

#include "emerick/app/project_state.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

// Styles keep their tempo to a tenth of a beat per minute, so a closer tempo counts as the same.
constexpr double kSameTempo = 0.05;

QString tempoText(double bpm) { return app::qs(std::format("{:g}", std::round(bpm * 10.0) / 10.0)); }

QString missingWarning(const ArrangerState& state) {
    return app::qs("The style file " + state.style + " is gone. Choose style to load it again from where it is now.");
}

}  // namespace

ArrangerStyles::ArrangerStyles(app::ProjectController& controller, app::Registry& registry)
    : controller_(controller), registry_(registry) {
    connect(&controller, &app::ProjectController::projectChanged, this, [this] {
        ++revision_;
        emit changed();
    });
}

const std::optional<StyleSummary>& ArrangerStyles::summaryOf(const std::filesystem::path& path) {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    auto found = cache_.find(path);
    if (found == cache_.end() || found->second.modified != modified) {
        found = cache_.insert_or_assign(path, Cached{.modified = modified, .summary = readStyle(path)}).first;
    }
    return found->second.summary;
}

QVariantMap ArrangerStyles::info(const QString& trackId) {
    const model::Track* track = controller_.project().track(model::TrackId{trackId.toStdString()});
    const auto state = track != nullptr ? arrangerState(*track) : std::nullopt;
    if (!state) {
        return {{"arranger", false}};
    }
    const double projectBpm = controller_.project().document().tempo.bpm;
    QVariantMap result{{"arranger", true}, {"loaded", false}, {"name", QString{}}, {"missing", QStringList{}}};
    if (state->style.empty()) {
        return result;
    }
    const std::optional<StyleSummary>& summary = summaryOf(state->style);
    result.insert("path", app::qs(state->style));
    result.insert("name", app::qs(std::filesystem::path{state->style}.stem().string()));
    if (!summary) {
        result.insert("warning", missingWarning(*state));
        return result;
    }
    QVariantList sections;
    QVariantList group;
    std::optional<Section> previous;
    for (const StyleSlot& slot : kStyleSlots) {
        if (previous.has_value() && previous != slot.kind) {
            sections.append(QVariant{group});
            group.clear();
        }
        group.append(QVariantMap{{"label", app::qs(std::string{slot.label})},
                                 {"present", stylePlays(summary->style, slot.kind, slot.variation)}});
        previous = slot.kind;
    }
    sections.append(QVariant{group});
    QStringList missing;
    for (const Section kind :
         {Section::Intro, Section::Main, Section::Fill, Section::FillTo, Section::Break, Section::Ending}) {
        for (int variation = 0; variation < variationCount(sectionInfo(kind)); ++variation) {
            if (!stylePlays(summary->style, kind, variation)) {
                missing.append(app::qs(sectionName({.at = 0, .kind = kind, .variation = variation, .bars = 0})));
            }
        }
    }
    result.insert("loaded", true);
    result.insert("bpm", summary->bpm);
    result.insert("meter",
                  app::qs(std::to_string(summary->meter.numerator) + "/" + std::to_string(summary->meter.denominator)));
    result.insert("sections", sections);
    result.insert("missing", missing);
    if (!state->hash.empty() && state->hash != summary->hash) {
        result.insert("warning", app::qs("The style file " + state->style +
                                         " changed since it was loaded. Choose style to load it again."));
    }
    if (std::abs(summary->bpm - projectBpm) >= kSameTempo) {
        result.insert("tempoWarning", "Style tempo " + tempoText(summary->bpm) + ", project " + tempoText(projectBpm) +
                                          ". Click to use the style tempo");
    }
    return result;
}

QVariantList ArrangerStyles::choices() {
    QVariantList result;
    for (const app::Json& style : styleList(registry_).at("styles")) {
        result.append(QVariantMap{{"id", app::qs(style.at("path").get<std::string>())},
                                  {"name", app::qs(style.at("name").get<std::string>())},
                                  {"bpm", style.at("bpm").get<double>()}});
    }
    return result;
}

}  // namespace emerick::yamaha
