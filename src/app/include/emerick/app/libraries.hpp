#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "emerick/app/nav.hpp"
#include "emerick/app/registry.hpp"

namespace emerick::app {

// A sound library Emerick downloads on request. The archive is a .tar.gz with one top folder, which every platform's
// tar extracts; sfz names its main .sfz, or the folder of .sfz files, inside that folder.
struct Library {
    std::string id;
    std::string name;
    std::string description;
    std::string url;
    std::string sha256;
    std::int64_t size = 0;
    std::string license;
    std::string sfz;
};

std::vector<Library> curatedLibraries();

// EMERICK_LIBRARY_DIR, else the libraries folder of a --profile run, else the user's app data folder.
std::filesystem::path libraryFolder(const QString& profileFolder);

class LibraryDownload;

// Installs and removes the libraries of a catalog, one subfolder each in the library folder. A download lands in a
// temporary file and an extraction in a temporary folder inside the library folder, which we rename into place only
// once both succeed, so a library is either fully installed or absent.
class Libraries : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)

public:
    Libraries(std::filesystem::path folder, std::vector<Library> catalog, QObject* parent = nullptr);
    ~Libraries() override;
    Libraries(const Libraries&) = delete;
    Libraries& operator=(const Libraries&) = delete;
    Libraries(Libraries&&) = delete;
    Libraries& operator=(Libraries&&) = delete;

    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }
    [[nodiscard]] const Library& find(const std::string& id) const;
    [[nodiscard]] bool installed(const Library& library) const;
    [[nodiscard]] bool busy(const std::string& id) const { return downloads_.contains(id); }
    [[nodiscard]] bool extracting(const std::string& id) const;
    [[nodiscard]] Json state(const Library& library) const;
    [[nodiscard]] Json listJson() const;
    [[nodiscard]] QVariantList list() const;

    // Starts the download; finished reports the outcome.
    void install(const Library& library);
    void cancel(const Library& library);
    // Blocks until tar exits, because QProcess reports its exit through a socket notifier that a wait skips.
    void waitForExtraction(const Library& library);
    void remove(const Library& library);

signals:
    void changed();
    void finished(const QString& id, const QString& error);

private:
    friend class LibraryDownload;
    void finish(const std::string& id, const std::string& error);

    std::filesystem::path folder_;
    std::vector<Library> catalog_;
    QNetworkAccessManager network_;
    std::map<std::string, std::unique_ptr<LibraryDownload>> downloads_;
    std::map<std::string, std::string> errors_;
};

// Registers library.list, library.install, library.cancel, library.remove and library.open over the catalog in
// folder, and the "libraries" state.
std::unique_ptr<Libraries> registerLibraries(Registry& registry, Nav& nav, std::filesystem::path folder,
                                             std::vector<Library> catalog = curatedLibraries());

}  // namespace emerick::app
