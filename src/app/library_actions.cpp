#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "emerick/app/event_wait.hpp"
#include "emerick/app/libraries.hpp"
#include "emerick/app/params.hpp"

namespace emerick::app {

namespace {

constexpr std::chrono::hours kInstallWait{3};

Json librarySchema(const Json& extra = Json::object()) {
    Json properties = {{"library", {{"type", "string"}, {"description", "the library id"}}}};
    properties.update(extra);
    return {{"required", {"library"}}, {"properties", properties}};
}

void checkChangesAllowed(const Registry& registry) {
    if (!registry.limits().libraryInstall) {
        throw AppError(
            "this service run refuses to install or remove libraries; start it with "
            "EMERICK_LIBRARY_INSTALL=1 or install them with --install-library");
    }
}

Json waitForInstall(Libraries& libraries, const Library& library) {
    processEventsUntil([&] { return !libraries.busy(library.id) || libraries.extracting(library.id); }, kInstallWait);
    libraries.waitForExtraction(library);
    Json state = libraries.state(library);
    if (libraries.busy(library.id)) {
        throw AppError(library.name + " is still downloading after " + std::to_string(kInstallWait.count()) + " h");
    }
    if (!state.at("installed").get<bool>()) {
        throw AppError(state.at("error").get<std::string>());
    }
    return state;
}

}  // namespace

std::unique_ptr<Libraries> registerLibraries(Registry& registry, Nav& nav, std::filesystem::path folder,
                                             std::vector<Library> catalog) {
    auto libraries = std::make_unique<Libraries>(std::move(folder), std::move(catalog));
    Libraries& owned = *libraries;
    const auto library = [&owned](const Json& params) -> const Library& {
        return owned.find(param<std::string>(params, "library"));
    };
    registry.addStateProvider("libraries", [&owned] { return owned.listJson(); });
    registry.addAction(
        {.id = "library.list",
         .title = "List sound libraries",
         .description =
             "Lists the sound libraries Emerick can download, with whether each is installed, where, its license, "
             "its main .sfz or folder of .sfz files, and its programs. A program is {file, gmSounds, transpose, "
             "gmKits, "
             "keys, controls, kitControls, pieces}: file is the .sfz to pass to sampler.load, empty for the main one; "
             "gmSounds names the General MIDI sounds it stands in for; transpose is the semitones to shift notes up. "
             "A drum kit program names the General MIDI kits it stands in for in gmKits, maps General MIDI drum notes "
             "to its own in keys [{gmNote, note}], starts at controls [{cc, value}], then applies the controls of the "
             "kit played from kitControls [{kit, controls}], and splits into pieces [{name, file, gmNotes}] to play "
             "each drum on its own track.",
         .aliases = {"sample libraries", "sfz libraries", "installed libraries"},
         .category = "Libraries",
         .run = [&owned](const Json&) {
             return Json{{"folder", owned.folder().string()}, {"libraries", owned.listJson()}};
         }});
    registry.addAction(
        {.id = "library.install",
         .title = "Install sound library",
         .description = "Downloads a sound library, checks its sha256 and extracts it into the library folder. It "
                        "returns while the download runs unless wait is true; library.list shows the progress.",
         .aliases = {"download library", "get samples"},
         .category = "Libraries",
         .schema =
             librarySchema({{"wait", {{"type", "boolean"}, {"description", "return once the library is installed"}}}}),
         .run = [&registry, &owned, library](const Json& params) {
             checkChangesAllowed(registry);
             const Library& picked = library(params);
             if (!owned.installed(picked) && !owned.busy(picked.id)) {
                 owned.install(picked);
             }
             if (optionalParam<bool>(params, "wait").value_or(false)) {
                 return waitForInstall(owned, picked);
             }
             return owned.state(picked);
         }});
    registry.addAction({.id = "library.cancel",
                        .title = "Cancel library download",
                        .aliases = {"stop download"},
                        .category = "Libraries",
                        .schema = librarySchema(),
                        .run = [&owned, library](const Json& params) {
                            const Library& picked = library(params);
                            owned.cancel(picked);
                            return owned.state(picked);
                        }});
    registry.addAction({.id = "library.remove",
                        .title = "Remove sound library",
                        .description = "Deletes an installed sound library's files; pass confirm: true.",
                        .aliases = {"delete library", "uninstall library"},
                        .category = "Libraries",
                        .schema = librarySchema({{"confirm", {{"type", "boolean"}}}}),
                        .run = [&registry, &owned, library](const Json& params) {
                            checkChangesAllowed(registry);
                            const Library& picked = library(params);
                            if (!optionalParam<bool>(params, "confirm").value_or(false)) {
                                throw AppError("pass confirm: true to delete the files of " + picked.name);
                            }
                            owned.remove(picked);
                            return owned.state(picked);
                        }});
    registry.addAction(
        {.id = "library.open",
         .title = "Show sound libraries",
         .description = "Opens the sound libraries dialog, scrolled to library when given, so the user can install "
                        "a library a feature needs.",
         .aliases = {"libraries", "download sounds", "sample libraries"},
         .category = "Libraries",
         .schema = {{"properties", {{"library", {{"type", "string"}, {"description", "the library to show"}}}}}},
         .run =
             [&nav, &owned](const Json& params) {
                 const std::optional<std::string> shown = optionalParam<std::string>(params, "library");
                 if (shown) {
                     (void)owned.find(*shown);
                 }
                 nav.setShownLibrary(qs(shown.value_or("")));
                 nav.setLibrariesOpen(true);
                 return Json{{"libraries", owned.listJson()}};
             },
         .menus = {kPluginsMenu},
         .windowOnly = true});
    return libraries;
}

}  // namespace emerick::app
