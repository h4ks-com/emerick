#include "emerick/app/project_files.hpp"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "emerick/app/audio_file.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_bundle.hpp"
#include "emerick/core/overloaded.hpp"

namespace emerick::app {

namespace {

struct PathKey {
    std::string_view pluginId;
    std::string_view key;
};

constexpr std::array kPluginPathKeys{PathKey{.pluginId = kGeneralMidiPluginId, .key = "soundFont"},
                                     PathKey{.pluginId = kVoicesPluginId, .key = "folder"},
                                     PathKey{.pluginId = kArrangerPluginId, .key = "style"}};

using PathRewrite = std::function<std::string(const std::string&)>;

void rewritePluginPath(model::PluginSlot& slot, const PathRewrite& rewrite) {
    const auto* const found = std::ranges::find(kPluginPathKeys, slot.pluginId, &PathKey::pluginId);
    if (found == kPluginPathKeys.end()) {
        return;
    }
    nlohmann::json state = nlohmann::json::parse(slot.state.begin(), slot.state.end(), nullptr, false);
    const std::string key{found->key};
    if (!state.is_object() || !state.contains(key) || !state.at(key).is_string()) {
        return;
    }
    const auto path = state.at(key).get<std::string>();
    std::string rewritten = path.empty() ? path : rewrite(path);
    if (rewritten == path) {
        return;
    }
    state[key] = std::move(rewritten);
    const std::string text = state.dump();
    slot.state.assign(text.begin(), text.end());
}

void rewriteTrackPaths(model::Track& track, const PathRewrite& rewrite) {
    if (track.instrument) {
        rewritePluginPath(*track.instrument, rewrite);
    }
    for (model::Insert& insert : track.inserts) {
        if (insert.plugin) {
            rewritePluginPath(*insert.plugin, rewrite);
        }
    }
}

model::Document withPluginPaths(model::Document document, const PathRewrite& rewrite) {
    for (model::Track& track : document.tracks) {
        rewriteTrackPaths(track, rewrite);
    }
    return document;
}

bool inside(const std::filesystem::path& path, const std::filesystem::path& folder) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, folder, error);
    return !error && !relative.empty() && *relative.begin() != "..";
}

std::filesystem::path copyInto(const std::filesystem::path& source, const std::filesystem::path& folder,
                               const Registry& registry) {
    const std::filesystem::path target = registry.checkedPath(freePath(folder, source).string(), PathAccess::Write);
    try {
        std::filesystem::create_directories(folder);
        std::filesystem::copy(source, target, std::filesystem::copy_options::recursive);
    } catch (const std::filesystem::filesystem_error& error) {
        throw AppError(error.what());
    }
    return target;
}

}  // namespace

void dropEmbeddedStyle(model::PluginSlot& slot) {
    nlohmann::json state = nlohmann::json::parse(slot.state.begin(), slot.state.end(), nullptr, false);
    if (slot.pluginId != kArrangerPluginId || !state.is_object() || !state.contains("data")) {
        return;
    }
    state.erase("data");
    const std::string text = state.dump();
    slot.state.assign(text.begin(), text.end());
}

model::Document withoutEmbeddedStyles(model::Document document) {
    for (model::Track& track : document.tracks) {
        if (track.instrument) {
            dropEmbeddedStyle(*track.instrument);
        }
        for (model::Insert& insert : track.inserts) {
            if (insert.plugin) {
                dropEmbeddedStyle(*insert.plugin);
            }
        }
    }
    return document;
}

model::Document withRelativePluginPaths(model::Document document, const std::filesystem::path& bundle) {
    if (!document.selfContained) {
        document = withoutEmbeddedStyles(std::move(document));
    }
    return withPluginPaths(std::move(document), [&](const std::string& path) {
        if (!std::filesystem::path{path}.is_absolute() || !inside(path, bundle.parent_path())) {
            return path;
        }
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(path, bundle, error);
        return error ? path : relative.generic_string();
    });
}

model::Document withAbsolutePluginPaths(model::Document document, const std::filesystem::path& bundle) {
    return withPluginPaths(std::move(document), [&](const std::string& path) {
        return std::filesystem::path{path}.is_absolute() ? path : normalizedPath(bundle / path).string();
    });
}

CollectedFiles collectFiles(Registry& registry, ProjectController& controller) {
    const std::filesystem::path bundle = controller.folder();
    const std::filesystem::path shipped = bundledClapFolder(QCoreApplication::applicationDirPath().toStdString());
    CollectedFiles collected;
    std::map<std::filesystem::path, std::filesystem::path> copies;
    const auto collect = [&](const std::filesystem::path& source, const std::filesystem::path& folder) {
        if (inside(source, bundle) || inside(source, shipped)) {
            return source;
        }
        if (!std::filesystem::exists(source)) {
            collected.missing.push_back(source.string());
            return source;
        }
        const std::filesystem::path readable = registry.checkedPath(source.string(), PathAccess::Read);
        if (const auto copied = copies.find(readable); copied != copies.end()) {
            return copied->second;
        }
        std::filesystem::path target = copyInto(readable, folder, registry);
        collected.copied.push_back(target.string());
        return copies.emplace(readable, std::move(target)).first->second;
    };
    const auto collectAudio = [&](model::AudioClip& audio) {
        const std::filesystem::path source = normalizedPath(bundle / audio.file);
        const std::filesystem::path target = collect(source, bundle / "audio");
        if (target != source) {
            audio.file = std::filesystem::relative(target, bundle).generic_string();
        }
    };
    const auto ids = controller.project().document().tracks | std::views::transform(&model::Track::id) |
                     std::ranges::to<std::vector>();
    controller.project().group("collect project files", [&] {
        for (const model::TrackId& id : ids) {
            controller.project().editTrack(id, "collect project files", [&](model::Track& track) {
                rewriteTrackPaths(track,
                                  [&](const std::string& path) { return collect(path, bundle / "files").string(); });
                for (model::Clip& clip : track.clips) {
                    std::visit(core::Overloaded{collectAudio, [](model::MidiClip&) {}}, clip);
                }
            });
        }
    });
    return collected;
}

}  // namespace emerick::app
