#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "style_library.hpp"

namespace emerick::yamaha {

// What the inspector and the Style lane show of a Style track's Arranger: its style and the sections that style plays.
class ArrangerStyles : public QObject {
    Q_OBJECT
    // Grows with every project change, so a binding that reads it follows the track's style.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    ArrangerStyles(app::ProjectController& controller, app::Registry& registry);

    [[nodiscard]] int revision() const { return revision_; }
    // Whether the track plays the Arranger, the style's name, tempo and meter, the project tempo, each section slot
    // with whether the style plays it, the section names the style lacks, and a warning for a style file that moved.
    Q_INVOKABLE QVariantMap info(const QString& trackId);
    // The styles in the style folders as the style picker lists them.
    Q_INVOKABLE QVariantList choices();

signals:
    void changed();

private:
    struct Cached {
        std::filesystem::file_time_type modified;
        std::string hash;
        std::optional<StyleSummary> summary;
    };
    const std::optional<StyleSummary>& summaryOf(const ArrangerState& state);

    app::ProjectController& controller_;
    app::Registry& registry_;
    std::map<std::filesystem::path, Cached> cache_;
    int revision_ = 0;
};

}  // namespace emerick::yamaha
