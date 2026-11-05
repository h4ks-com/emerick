#include "remote_peaks.hpp"

#include <QBuffer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>

namespace emerick::viewer {

namespace {

constexpr std::chrono::seconds kRetryDelay{2};
constexpr int kAttempts = 150;

struct Request {
    QUrl bundle;
    std::filesystem::path file;
};

}  // namespace

void RemotePeaks::setBundle(const QUrl& bundle) {
    bundle_ = bundle;
    if (!bundle_.path().endsWith('/')) {
        bundle_.setPath(bundle_.path() + '/');
    }
    files_.clear();
    failures_.clear();
}

const app::PeakLevels* RemotePeaks::levels(const std::filesystem::path& file) {
    if (const auto found = files_.find(file); found != files_.end()) {
        return found->second.get();
    }
    files_.emplace(file, nullptr);
    fetch(file);
    return nullptr;
}

void RemotePeaks::fetch(const std::filesystem::path& file) {
    QUrl relative;
    relative.setPath(QString::fromStdString(app::peakFile(file).generic_string()));
    QNetworkReply* reply = network_.get(QNetworkRequest{bundle_.resolved(relative)});
    const auto request = std::make_shared<const Request>(bundle_, file);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request] {
        reply->deleteLater();
        if (request->bundle != bundle_) {
            return;
        }
        QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        QBuffer buffer{&bytes};
        buffer.open(QIODevice::ReadOnly);
        std::optional<app::PeakFile> stored = app::readPeakFile(buffer);
        if (!stored && ++failures_[request->file] < kAttempts) {
            QTimer::singleShot(kRetryDelay, this, [this, request] {
                if (request->bundle == bundle_ && files_.contains(request->file)) {
                    fetch(request->file);
                }
            });
            return;
        }
        files_[request->file] =
            std::make_unique<app::PeakLevels>(stored ? app::buildLevels(std::move(stored->peaks)) : app::PeakLevels{});
        emit loaded();
    });
}

}  // namespace emerick::viewer
