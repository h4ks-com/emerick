#include "emerick/app/libraries.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtEnvironmentVariables>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>

#include "emerick/app/path_access.hpp"
#include "emerick/core/join.hpp"

namespace emerick::app {

namespace {

const Library& findLibrary(const std::vector<Library>& catalog, const std::string& id) {
    const auto found = std::ranges::find(catalog, id, &Library::id);
    if (found == catalog.end()) {
        throw AppError("no library " + id + "; pick one of " +
                       core::join(catalog | std::views::transform(&Library::id), ", "));
    }
    return *found;
}

constexpr qint64 kProgressIntervalMs = 100;

Json controlsJson(const std::vector<ControlValue>& controls) {
    return controls | std::views::transform([](const ControlValue& control) {
               return Json{{"cc", control.cc}, {"value", control.value}};
           }) |
           std::ranges::to<Json::array_t>();
}

Json programJson(const LibraryProgram& program) {
    return {{"file", program.file},
            {"gmSounds", program.gmSounds},
            {"transpose", program.transpose},
            {"gmKits", program.gmKits},
            {"keys", program.keys | std::views::transform([](const KeyMapping& key) {
                         return Json{{"gmNote", key.gmNote}, {"note", key.note}};
                     }) | std::ranges::to<Json::array_t>()},
            {"controls", controlsJson(program.controls)},
            {"kitControls", program.kitControls | std::views::transform([](const KitControls& kit) {
                                return Json{{"kit", kit.kit}, {"controls", controlsJson(kit.controls)}};
                            }) | std::ranges::to<Json::array_t>()},
            {"pieces", program.pieces | std::views::transform([](const DrumPiece& piece) {
                           return Json{{"name", piece.name}, {"file", piece.file}, {"gmNotes", piece.gmNotes}};
                       }) | std::ranges::to<Json::array_t>()}};
}

}  // namespace

std::filesystem::path libraryFolder(const QString& profileFolder) {
    if (const QString folder = qEnvironmentVariable("EMERICK_LIBRARY_DIR"); !folder.isEmpty()) {
        return folder.toStdString();
    }
    const QString base = profileFolder.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                             : profileFolder;
    return std::filesystem::path{base.toStdString()} / "libraries";
}

// UI thread: downloads one library into a temporary file while hashing it, then extracts it with the system tar.
class LibraryDownload : public QObject {
public:
    LibraryDownload(Libraries& owner, QNetworkAccessManager& network, Library library)
        : owner_(owner), library_(std::move(library)), part_(qs(partPath().string())) {
        std::error_code ignored;
        std::filesystem::remove_all(stagingPath(), ignored);
        if (!part_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            throw AppError("cannot write " + partPath().string() + ": " + part_.errorString().toStdString());
        }
        reply_ = network.get(QNetworkRequest(QUrl(qs(library_.url))));
        // The manager deletes its replies with itself, which can come before this download's deleteLater.
        reply_->setParent(this);
        connect(reply_, &QNetworkReply::readyRead, this, [this] { receive(); });
        connect(reply_, &QNetworkReply::finished, this, [this] { downloaded(); });
        progress_.start();
    }
    LibraryDownload(const LibraryDownload&) = delete;
    LibraryDownload& operator=(const LibraryDownload&) = delete;
    LibraryDownload(LibraryDownload&&) = delete;
    LibraryDownload& operator=(LibraryDownload&&) = delete;

    ~LibraryDownload() override {
        reply_->disconnect(this);
        if (tar_) {
            tar_->disconnect(this);
            tar_->kill();
            tar_->waitForFinished();
        }
    }

    [[nodiscard]] qint64 received() const { return received_; }
    [[nodiscard]] bool extracting() const { return tar_ != nullptr; }
    void waitForExtraction() {
        if (tar_) {
            tar_->waitForFinished(-1);
        }
    }

    void cancel() { stop("the download of " + library_.name + " was cancelled"); }

private:
    [[nodiscard]] std::filesystem::path partPath() const { return owner_.folder() / ("." + library_.id + ".download"); }
    [[nodiscard]] std::filesystem::path stagingPath() const {
        return owner_.folder() / ("." + library_.id + ".extracting");
    }

    void stop(std::string error) {
        error_ = std::move(error);
        if (tar_) {
            tar_->kill();
        } else {
            reply_->abort();
        }
    }

    void receive() {
        const QByteArray chunk = reply_->readAll();
        hash_.addData(chunk);
        received_ += chunk.size();
        if (part_.write(chunk) != chunk.size()) {
            stop("cannot write " + partPath().string() + ": " + part_.errorString().toStdString());
            return;
        }
        if (progress_.hasExpired(kProgressIntervalMs)) {
            progress_.restart();
            emit owner_.changed();
        }
    }

    void downloaded() {
        if (error_.empty() && reply_->error() != QNetworkReply::NoError) {
            error_ = "cannot download " + library_.name + ": " + reply_->errorString().toStdString();
        }
        if (!error_.empty()) {
            done();
            return;
        }
        receive();
        part_.close();
        if (hash_.result().toHex().toStdString() != library_.sha256) {
            error_ = "the download of " + library_.name + " does not match its sha256, so we deleted it; try again";
            done();
            return;
        }
        std::error_code failed;
        std::filesystem::create_directories(stagingPath(), failed);
        if (failed) {
            error_ = "cannot create " + stagingPath().string() + ": " + failed.message();
            done();
            return;
        }
        tar_ = std::make_unique<QProcess>();
        connect(tar_.get(), &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus status) { extracted(code, status); });
        connect(tar_.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                error_ = "cannot run tar to extract " + library_.name + ", so install tar";
                done();
            }
        });
        tar_->start("tar", {"-xzf", qs(partPath().string()), "-C", qs(stagingPath().string()), "--strip-components=1"});
        emit owner_.changed();
    }

    void extracted(int code, QProcess::ExitStatus status) {
        if (error_.empty() && (status != QProcess::NormalExit || code != 0)) {
            error_ = "cannot extract " + library_.name + ": " + tar_->readAllStandardError().trimmed().toStdString();
        }
        if (error_.empty()) {
            std::error_code failed;
            std::filesystem::rename(stagingPath(), owner_.folder() / library_.id, failed);
            if (failed) {
                error_ = "cannot move " + library_.name + " into place: " + failed.message();
            }
        }
        done();
    }

    void done() {
        part_.close();
        std::error_code ignored;
        std::filesystem::remove(partPath(), ignored);
        std::filesystem::remove_all(stagingPath(), ignored);
        owner_.finish(library_.id, error_);
    }

    Libraries& owner_;
    Library library_;
    QFile part_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QProcess> tar_;
    QElapsedTimer progress_;
    qint64 received_ = 0;
    std::string error_;
};

Libraries::Libraries(std::filesystem::path folder, std::vector<Library> catalog, QObject* parent)
    : QObject(parent), folder_(std::move(folder)), catalog_(std::move(catalog)) {}

Libraries::~Libraries() = default;

const Library& Libraries::find(const std::string& id) const { return findLibrary(catalog_, id); }

std::filesystem::path librarySfz(const std::filesystem::path& folder, const std::string& id, const std::string& file) {
    const std::vector<Library> catalog = curatedLibraries();
    const Library& library = findLibrary(catalog, id);
    if (!std::filesystem::is_directory(folder / library.id)) {
        throw AppError(library.name + " is not installed; install it with library.install");
    }
    const std::filesystem::path root = std::filesystem::canonical(folder / library.id);
    const std::optional<std::filesystem::path> chosen = pathInside(root / (file.empty() ? library.sfz : file), root);
    if (!chosen) {
        throw AppError(file + " is outside the " + library.name + " library");
    }
    if (std::filesystem::is_directory(*chosen)) {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(*chosen)) {
            if (entry.path().extension() == ".sfz") {
                names.push_back(std::filesystem::relative(entry.path(), root).generic_string());
            }
        }
        std::ranges::sort(names);
        throw AppError("pick an .sfz file of " + library.name + " with file, such as " +
                       core::join(names | std::views::take(40), ", "));
    }
    return *chosen;
}

bool Libraries::installed(const Library& library) const {
    std::error_code ignored;
    return std::filesystem::is_directory(folder_ / library.id, ignored);
}

Json Libraries::state(const Library& library) const {
    const auto download = downloads_.find(library.id);
    const bool downloading = download != downloads_.end();
    const auto phase = [&]() -> const char* {
        if (extracting(library.id)) {
            return "extracting";
        }
        if (downloading) {
            return "downloading";
        }
        return installed(library) ? "installed" : "available";
    };
    const auto error = errors_.find(library.id);
    return {{"id", library.id},
            {"name", library.name},
            {"description", library.description},
            {"size", library.size},
            {"license", library.license},
            {"installed", installed(library)},
            {"state", phase()},
            {"received", downloading ? download->second->received() : 0},
            {"error", error == errors_.end() ? std::string{} : error->second},
            {"path", (folder_ / library.id).string()},
            {"sfz", (folder_ / library.id / library.sfz).string()},
            {"url", library.url},
            {"programs", library.programs | std::views::transform(programJson) | std::ranges::to<Json::array_t>()}};
}

Json Libraries::listJson() const {
    return catalog_ | std::views::transform([this](const Library& library) { return state(library); }) |
           std::ranges::to<Json::array_t>();
}

QVariantList Libraries::list() const { return toVariant(listJson()).toList(); }

void Libraries::install(const Library& library) {
    if (busy(library.id)) {
        throw AppError(library.name + " is downloading already");
    }
    std::error_code failed;
    std::filesystem::create_directories(folder_, failed);
    if (failed) {
        throw AppError("cannot create the library folder " + folder_.string() + ": " + failed.message());
    }
    errors_.erase(library.id);
    downloads_.emplace(library.id, std::make_unique<LibraryDownload>(*this, network_, library));
    emit changed();
}

bool Libraries::extracting(const std::string& id) const {
    const auto download = downloads_.find(id);
    return download != downloads_.end() && download->second->extracting();
}

void Libraries::waitForExtraction(const Library& library) {
    if (const auto download = downloads_.find(library.id); download != downloads_.end()) {
        download->second->waitForExtraction();
    }
}

void Libraries::cancel(const Library& library) {
    const auto download = downloads_.find(library.id);
    if (download == downloads_.end()) {
        throw AppError(library.name + " is not downloading");
    }
    download->second->cancel();
}

void Libraries::remove(const Library& library) {
    if (busy(library.id)) {
        throw AppError(library.name + " is downloading; cancel the download first");
    }
    if (!installed(library)) {
        throw AppError(library.name + " is not installed");
    }
    std::error_code failed;
    std::filesystem::remove_all(folder_ / library.id, failed);
    errors_.erase(library.id);
    emit changed();
    if (failed) {
        throw AppError("cannot remove " + (folder_ / library.id).string() + ": " + failed.message());
    }
}

void Libraries::finish(const std::string& id, const std::string& error) {
    auto node = downloads_.extract(id);
    // We are inside a slot of the download, so we delete it once that slot returns.
    node.mapped().release()->deleteLater();
    if (!error.empty()) {
        errors_[id] = error;
    }
    emit changed();
    emit finished(qs(id), qs(error));
}

}  // namespace emerick::app
