#pragma once

#include <QNetworkAccessManager>
#include <QUrl>
#include <filesystem>
#include <map>
#include <memory>

#include "emerick/app/peak_levels.hpp"

namespace emerick::viewer {

// Fetches the peak files the app writes beside a bundle's audio, "<bundle>/audio/.peaks/<name>.peaks", over HTTP or
// from a file URL. A clip's file is a path inside the bundle.
class RemotePeaks : public app::PeakSource {
    Q_OBJECT

public:
    using PeakSource::PeakSource;

    void setBundle(const QUrl& bundle);
    const app::PeakLevels* levels(const std::filesystem::path& file) override;

private:
    QUrl bundle_;
    QNetworkAccessManager network_;
    // A null entry means the file is loading.
    std::map<std::filesystem::path, std::unique_ptr<app::PeakLevels>> files_;
};

}  // namespace emerick::viewer
