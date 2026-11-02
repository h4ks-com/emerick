#include "remote_peaks.hpp"

#include <QBuffer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <optional>
#include <utility>

namespace emerick::viewer {

void RemotePeaks::setBundle(const QUrl& bundle) {
    bundle_ = bundle;
    if (!bundle_.path().endsWith('/')) {
        bundle_.setPath(bundle_.path() + '/');
    }
    files_.clear();
}

const app::PeakLevels* RemotePeaks::levels(const std::filesystem::path& file) {
    if (const auto found = files_.find(file); found != files_.end()) {
        return found->second.get();
    }
    files_.emplace(file, nullptr);
    QUrl relative;
    relative.setPath(QString::fromStdString(app::peakFile(file).generic_string()));
    QNetworkReply* reply = network_.get(QNetworkRequest{bundle_.resolved(relative)});
    connect(reply, &QNetworkReply::finished, this, [this, reply, path = QString::fromStdString(file.string())] {
        QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        reply->deleteLater();
        QBuffer buffer{&bytes};
        buffer.open(QIODevice::ReadOnly);
        std::optional<app::PeakFile> stored = app::readPeakFile(buffer);
        files_[path.toStdString()] =
            std::make_unique<app::PeakLevels>(stored ? app::buildLevels(std::move(stored->peaks)) : app::PeakLevels{});
        emit loaded();
    });
    return nullptr;
}

}  // namespace emerick::viewer
