#include "emerick/app/project_bundle.hpp"

#include <QStandardPaths>
#include <algorithm>
#include <set>
#include <system_error>
#include <variant>

#include "emerick/app/peak_cache.hpp"
#include "emerick/core/overloaded.hpp"
#include "emerick/model/json.hpp"

namespace emerick::app {

namespace {

constexpr const char* kBundleSuffix = ".erk";

}  // namespace

std::filesystem::path defaultProjectsFolder() {
    return std::filesystem::path{QStandardPaths::writableLocation(QStandardPaths::MusicLocation).toStdString()} /
           "Emerick";
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::filesystem::path result = std::filesystem::absolute(path).lexically_normal();
    return result.has_filename() ? result : result.parent_path();
}

std::filesystem::path bundleOf(const std::filesystem::path& path) {
    std::filesystem::path result = normalizedPath(path);
    return result.filename() == "project.json" ? result.parent_path() : result;
}

std::filesystem::path namedBundle(const std::filesystem::path& path) {
    std::filesystem::path result = normalizedPath(path);
    if (result.extension() != kBundleSuffix) {
        result += kBundleSuffix;
    }
    return result;
}

std::filesystem::path nextUntitledBundle(const std::filesystem::path& folder) {
    std::filesystem::path bundle;
    for (int number = 1; bundle.empty() || std::filesystem::exists(bundle); ++number) {
        bundle = folder / ("Untitled " + std::to_string(number) + kBundleSuffix);
    }
    return bundle;
}

bool holdsNothing(const model::Document& document, const std::filesystem::path& bundle) {
    std::error_code error;
    const std::filesystem::path audio = bundle / "audio";
    const bool noAudio = !std::filesystem::exists(audio, error) || std::filesystem::is_empty(audio, error);
    return model::toJson(document) == model::toJson(model::Document{}) && noAudio;
}

// A rename fails across file systems, so there we copy the bundle and delete the original. Once the copy is complete
// the project lives at to, so we keep going when the original cannot be deleted.
void moveBundle(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        std::filesystem::copy(from, to, std::filesystem::copy_options::recursive);
        std::filesystem::remove_all(from, error);
    }
}

std::vector<std::string> removeUnusedAudio(const model::Document& document, const std::filesystem::path& bundle) {
    std::set<std::filesystem::path> used;
    for (const model::Track& track : document.tracks) {
        for (const model::Clip& clip : track.clips) {
            std::visit(core::Overloaded{[&](const model::AudioClip& audio) {
                                            used.insert((bundle / audio.file).lexically_normal());
                                        },
                                        [](const model::MidiClip&) {}},
                       clip);
        }
    }
    std::vector<std::string> removed;
    for (const auto& entry : std::filesystem::directory_iterator(bundle / "audio")) {
        if (entry.is_regular_file() && !used.contains(entry.path().lexically_normal())) {
            std::filesystem::remove(entry.path());
            std::filesystem::remove(peakFile(entry.path()));
            removed.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(removed);
    return removed;
}

}  // namespace emerick::app
