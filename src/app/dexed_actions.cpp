#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "emerick/app/action_params.hpp"
#include "emerick/app/dexed.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/core/fuzzy.hpp"

namespace emerick::app {

namespace {

QFileInfoList shippedCartridges() { return QDir(":/dexed").entryInfoList(QDir::Files, QDir::Name); }

std::string cartridgeId(const QFileInfo& file) { return file.completeBaseName().toLower().toStdString(); }

std::vector<std::uint8_t> cartridgeBytes(const Registry& registry, const std::string& given) {
    const QFileInfoList shipped = shippedCartridges();
    const std::string id = core::toLower(given);
    if (const auto found = std::ranges::find(shipped, id, cartridgeId); found != shipped.end()) {
        QFile file(found->filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            throw AppError("cannot read the cartridge " + given);
        }
        const QByteArray bytes = file.readAll();
        return {bytes.begin(), bytes.end()};
    }
    std::ifstream file(registry.checkedPath(given, PathAccess::Read), std::ios::binary);
    if (!file) {
        throw AppError("no cartridge " + given + ", pass a .syx file or a cartridge id from dexed.programs");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

Dx7Cartridge readCartridge(const Registry& registry, const std::string& given) {
    auto cartridge = parseDx7Cartridge(cartridgeBytes(registry, given));
    if (!cartridge) {
        throw AppError(given + " holds no DX7 32-voice cartridge, pass a .syx file with a 4104-byte bulk dump");
    }
    return *std::move(cartridge);
}

DexedState dexedState(ProjectController& controller, const model::Track& track) {
    if (!track.instrument || track.instrument->pluginId != kDexedPluginId) {
        throw AppError("track " + track.name + " does not play Dexed, run dexed.load on it first");
    }
    std::vector<std::uint8_t> bytes = track.instrument->state;
    if (bytes.empty()) {
        syncInstruments(controller);
        if (const auto live = controller.instruments().find(track.id)) {
            bytes = live->saveState().value_or(bytes);
        }
    }
    auto state = parseDexedState(bytes);
    if (!state) {
        throw AppError("cannot read the Dexed settings of track " + track.name + ", see the track's warning");
    }
    return *std::move(state);
}

std::size_t programIndex(const Json& given, const std::vector<std::string>& names) {
    if (given.is_string()) {
        const auto choices = names |
                             std::views::transform([](const std::string& name) { return std::string_view{name}; }) |
                             std::ranges::to<std::vector>();
        return choiceIndex("program", choices, given.get<std::string>(), "dexed.programs lists the names");
    }
    const auto number = given.is_number_integer() ? given.get<std::int64_t>() : 0;
    if (number < 1 || std::cmp_greater(number, names.size())) {
        throw AppError("program takes a number from 1 to 32 or a name from dexed.programs");
    }
    return static_cast<std::size_t>(number - 1);
}

Json shippedCartridgeCredit(const QFileInfo& file) {
    return {{"id", cartridgeId(file)},
            {"name", file.completeBaseName().toStdString()},
            {"license", "GPL-3.0"},
            {"url", "https://github.com/asb2m10/dexed/tree/v1.0.1"}};
}

Json programList(const std::vector<std::string>& names, const Dx7Cartridge& cartridge) {
    const auto programs =
        std::views::iota(std::size_t{0}, names.size()) |
        std::views::transform([&](std::size_t index) { return Json{{"number", index + 1}, {"name", names[index]}}; }) |
        std::ranges::to<std::vector>();
    return {{"programs", programs},
            {"cartridges", shippedCartridges() | std::views::transform(cartridgeId) | std::ranges::to<std::vector>()},
            {"credit", cartridgeCredit(cartridge)},
            {"message", std::to_string(names.size()) + " programs"}};
}

}  // namespace

Json shippedCartridgeCredits() {
    return shippedCartridges() | std::views::transform(shippedCartridgeCredit) | std::ranges::to<Json::array_t>();
}

Json cartridgeCredit(const Dx7Cartridge& cartridge) {
    const QFileInfoList shipped = shippedCartridges();
    const auto found = std::ranges::find_if(shipped, [&](const QFileInfo& file) {
        QFile bytes(file.filePath());
        if (!bytes.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray read = bytes.readAll();
        const auto parsed = parseDx7Cartridge(std::vector<std::uint8_t>(read.begin(), read.end()));
        return parsed && parsed->sysex == cartridge.sysex;
    });
    return found == shipped.end() ? Json{} : shippedCartridgeCredit(*found);
}

void registerDexedActions(Registry& registry, ProjectController& controller, Nav& nav, PluginCatalog& catalog) {
    const Json cartridgeSchema = {
        {"type", "string"}, {"description", "a .syx file, or the id of a cartridge Emerick ships, see dexed.programs"}};
    registry.addAction(
        {.id = "dexed.programs",
         .title = "List DX7 programs",
         .description =
             "Lists the 32 programs of the cartridge the track's Dexed plays, by number and name, with the "
             "one it plays, or the programs of a cartridge. Also lists the ids of the cartridges Emerick ships, and "
             "the credit {id, name, license, url} of the cartridge when Emerick ships it.",
         .aliases = {"dx7 patches", "dexed presets", "list fm sounds"},
         .category = "Tracks",
         .schema = {{"properties", {{"track", {{"type", "string"}}}, {"cartridge", cartridgeSchema}}}},
         .run =
             [&registry, &controller, &nav](const Json& params) {
                 if (params.contains("cartridge")) {
                     const Dx7Cartridge cartridge = readCartridge(registry, param<std::string>(params, "cartridge"));
                     Json listed = programList(dx7ProgramNames(cartridge), cartridge);
                     listed["cartridge"] = params.at("cartridge");
                     return listed;
                 }
                 const model::TrackId id = midiTrack(registry, controller, nav, params);
                 const DexedState state = dexedState(controller, *controller.project().track(id));
                 Json listed = programList(dx7ProgramNames(state.cartridge), state.cartridge);
                 listed["track"] = id.value;
                 listed["program"] = state.program + 1;
                 return listed;
             },
         .menus = {kTrackContextMenu},
         .notifies = true});
    registry.addAction(
        {.id = "dexed.load",
         .title = "Load DX7 program",
         .description = "Plays a DX7 program on the MIDI track through Dexed, loading Dexed first when the track plays "
                        "another instrument, as one undo step. Pass a program by number or name, and a cartridge to "
                        "load first, which starts at program 1. The track's saved Dexed settings keep both.",
         .aliases = {"dx7 patch", "load syx", "pick fm sound", "dexed cartridge"},
         .category = "Tracks",
         .schema = {{"properties",
                     {{"track", {{"type", "string"}}},
                      {"program",
                       {{"type", {"integer", "string"}},
                        {"description", "a number from 1 to 32 or a name from dexed.programs"}}},
                      {"cartridge", cartridgeSchema}}}},
         .run =
             [&registry, &controller, &nav, &catalog](const Json& params) {
                 const model::TrackId id = midiTrack(registry, controller, nav, params);
                 const auto cartridge =
                     optionalParam<std::string>(params, "cartridge").transform([&](const std::string& given) {
                         return readCartridge(registry, given);
                     });
                 if (!cartridge && !params.contains("program")) {
                     throw AppError("pass program, a number from 1 to 32 or a name from dexed.programs, or cartridge");
                 }
                 Json result;
                 controller.project().group("load DX7 program", [&] {
                     if (const model::Track& track = *controller.project().track(id);
                         !track.instrument || track.instrument->pluginId != kDexedPluginId) {
                         loadInstrument(registry, controller, id,
                                        slotFor(registry, catalog, {{"plugin", kDexedPluginId}}));
                     }
                     const DexedState state = dexedState(controller, *controller.project().track(id));
                     const Dx7Cartridge& played = cartridge ? *cartridge : state.cartridge;
                     const auto names = dx7ProgramNames(played);
                     const std::size_t program =
                         params.contains("program") ? programIndex(params.at("program"), names) : 0;
                     editTrack(controller, id, "load DX7 program " + names[program], [&](model::Track& edited) {
                         edited.instrument->state = dexedStateWith(state, played, program);
                     });
                     result = {{"track", id.value},
                               {"program", program + 1},
                               {"name", names[program]},
                               {"message", "Loaded " + names[program]}};
                 });
                 syncInstruments(controller);
                 return result;
             },
         .menus = {kTrackContextMenu},
         .dialog = FileDialog{.param = "cartridge", .nameFilter = "DX7 cartridges (*.syx)"}});
}

}  // namespace emerick::app
