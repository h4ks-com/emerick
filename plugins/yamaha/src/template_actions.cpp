#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/yamaha/keyboard_template.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

namespace fs = std::filesystem;
using app::AppError;
using app::Json;

constexpr const char* kTemplateFile = "Emerick.mis";
constexpr const char* kInstallSteps =
    "Turn the keyboard off, then turn it on while holding PLAYLIST so its User drive mounts on this computer. "
    "Press OK to copy the Emerick template to it. Then eject the drive, restart the keyboard, open Menu > MIDI and "
    "pick Emerick on the User tab.";

constexpr const char* kAfterInstall =
    "Eject the drive, restart the keyboard, open Menu > MIDI and pick Emerick on the User tab.";

bool isKeyboardDrive(const fs::path& folder) {
    std::error_code error;
    return std::ranges::all_of(std::array{"MIDI", "STYLE", "REGIST"},
                               [&](const char* name) { return fs::is_directory(folder / name, error); });
}

// A drive can go away while we scan, so we stop at the first error.
std::vector<fs::path> subfolders(const fs::path& folder) {
    std::vector<fs::path> paths;
    std::error_code error;
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error)) {
        paths.push_back(entry->path());
    }
    return paths;
}

// ponytail: macOS and Linux mount points only; a Windows drive letter has to come in as the drive param.
std::optional<fs::path> mountedKeyboardDrive() {
    for (const fs::path root : {"/Volumes", "/media", "/run/media"}) {
        for (const fs::path& volume : subfolders(root)) {
            // We look one level deeper because Linux mounts a drive under a folder named after the user.
            std::vector<fs::path> candidates = subfolders(volume);
            candidates.insert(candidates.begin(), volume);
            if (const auto drive = std::ranges::find_if(candidates, isKeyboardDrive); drive != candidates.end()) {
                return *drive;
            }
        }
    }
    return std::nullopt;
}

void writeTemplate(const app::Registry& registry, const fs::path& path) {
    const std::vector<std::uint8_t> bytes =
        encodeKeyboardTemplate({.controllerChannel = registry.settingValue(kControllerChannelSetting).get<int>()});
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw AppError("cannot write " + path.string());
    }
}

}  // namespace

void registerTemplateActions(app::Registry& registry) {
    registry.addAction(
        {.id = "yamaha.template.install",
         .title = "Install keyboard template",
         .description = std::string{"Copies the Emerick MIDI template into the MIDI folder of the keyboard's User "
                                    "drive, found among the mounted drives or passed as drive. "} +
                        kInstallSteps,
         .aliases = {"install midi template", "set up keyboard midi"},
         .keywords = {"genos", "yamaha", "mis"},
         .category = "Yamaha",
         .schema = {{"properties", {{"drive", {{"type", "string"}}}}}},
         .run =
             [&registry](const Json& params) {
                 const auto drive = app::optionalParam<std::string>(params, "drive")
                                        .transform([](const std::string& path) { return fs::path{path}; })
                                        .or_else(mountedKeyboardDrive);
                 if (!drive) {
                     throw AppError(
                         "no keyboard User drive is mounted, turn the keyboard on while holding PLAYLIST or pass "
                         "drive");
                 }
                 if (!isKeyboardDrive(*drive)) {
                     throw AppError(drive->string() +
                                    " has no MIDI, STYLE and REGIST folders, pass the keyboard's "
                                    "User drive");
                 }
                 const fs::path path =
                     registry.checkedPath((*drive / "MIDI" / kTemplateFile).string(), app::PathAccess::Write);
                 writeTemplate(registry, path);
                 return Json{{"path", path.string()},
                             {"steps", kInstallSteps},
                             {"message", "Copied the template to " + path.string() + ". " + kAfterInstall}};
             },
         .menus = {kYamahaMenu},
         .confirm = kInstallSteps,
         .notifies = true});
    registry.addAction(
        {.id = "yamaha.template.save",
         .title = "Save keyboard template",
         .description = "Saves the Emerick MIDI template as a file to copy into the MIDI folder of the "
                        "keyboard's User drive or a USB stick.",
         .aliases = {"export midi template"},
         .keywords = {"genos", "yamaha", "mis"},
         .category = "Yamaha",
         .schema = {{"required", {"path"}}, {"properties", {{"path", {{"type", "string"}}}}}},
         .run =
             [&registry](const Json& params) {
                 const fs::path path =
                     registry.checkedPath(app::param<std::string>(params, "path"), app::PathAccess::Write);
                 writeTemplate(registry, path);
                 return Json{{"path", path.string()}, {"message", "Saved the template to " + path.string()}};
             },
         .menus = {kYamahaMenu},
         .dialog = app::FileDialog{.param = "path",
                                   .mode = app::FileDialogMode::Save,
                                   .nameFilter = "MIDI templates (*.mis)",
                                   .suffix = "mis"},
         .notifies = true});
}

}  // namespace emerick::yamaha
