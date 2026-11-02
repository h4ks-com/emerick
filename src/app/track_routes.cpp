#include "emerick/app/track_routes.hpp"

#include "emerick/core/lookup.hpp"

namespace emerick::app {

std::optional<RouteSections> coreRoutes(std::string_view kind) {
    const auto* const found = core::findEntry(kCoreTrackKinds, kind, &CoreTrackKind::kind);
    return found == nullptr ? std::nullopt : std::optional{found->routes};
}

}  // namespace emerick::app
