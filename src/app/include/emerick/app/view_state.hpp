#pragma once

#include "emerick/app/nav.hpp"
#include "emerick/app/variant.hpp"

namespace emerick::app {

// The timeline view a project saves and opens with: zoom, scroll, track height and the automation lanes on show, so
// the app, a headless session, a video and the viewer all show a project the way its author left it.
Json viewState(const Nav& nav);
// Leaves out what the state does not hold or holds with the wrong type.
void applyViewState(Nav& nav, const Json& state);

}  // namespace emerick::app
