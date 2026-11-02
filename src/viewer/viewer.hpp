#pragma once

#include <QByteArray>
#include <QString>

#include "emerick/app/nav.hpp"
#include "emerick/app/transport_clock.hpp"
#include "remote_peaks.hpp"
#include "silent_meters.hpp"
#include "viewer_project.hpp"
#include "viewer_registry.hpp"
#include "viewer_tracks.hpp"

namespace emerick::viewer {

// Everything the shared views bind to, registered under the app's Emerick.App names.
struct Viewer {
    Viewer();

    ViewerProject project;
    ViewerTracks tracks{project};
    app::Nav nav;
    ViewerRegistry registry{project, nav};
    SilentMeters meters;
    RemotePeaks peaks;
    app::TransportClock clock;

    // Call it once per process, before loading a view.
    void exposeToQml();
    // Loads project.json bytes; returns the error text, or an empty string.
    QString load(const QByteArray& json, const QString& name);
};

}  // namespace emerick::viewer
