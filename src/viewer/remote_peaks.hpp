#pragma once

#include <QNetworkAccessManager>
#include <QUrl>
#include <filesystem>
#include <map>
#include <memory>

#include "emerick/app/peak_levels.hpp"

namespace emerick::viewer {

// Fetches the peak files the app writes beside a bundle's audio, "<bundle>/audio/.peaks/<name>.peaks", over HTTP or
// from a file URL. A clip's file is a path inside the bundle. A missing or unreadable file is fetched again for a
// while, because the service writes the peaks in the background while the first viewer of a bundle loads, and a
// browser fetch sometimes fails.
class RemotePeaks : public app::PeakSource {
    Q_OBJECT

public:
    using PeakSource::PeakSource;

    void setBundle(const QUrl& bundle);
    const app::PeakLevels* levels(const std::filesystem::path& file) override;

private:
    void fetch(const std::filesystem::path& file);

    QUrl bundle_;
    QNetworkAccessManager network_;
    // A null entry means the file is loading.
    std::map<std::filesystem::path, std::unique_ptr<app::PeakLevels>> files_;
    std::map<std::filesystem::path, int> failures_;
};

}  // namespace emerick::viewer
