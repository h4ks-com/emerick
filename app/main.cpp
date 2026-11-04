#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QTimer>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtSystemDetection>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "emerick/app/backends.hpp"
#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/key_focus.hpp"
#include "emerick/app/libraries.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/mcp_server.hpp"
#include "emerick/app/mcp_tools.hpp"
#include "emerick/app/monitor_actions.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/peak_cache.hpp"
#include "emerick/app/plugin_actions.hpp"
#include "emerick/app/plugin_catalog.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/script_library.hpp"
#include "emerick/app/track_list_model.hpp"
#include "emerick/app/ui_api.hpp"
#include "emerick/app/view_engine.hpp"
#include "emerick/engine/plugin.hpp"
#include "emerick/yamaha/plugin.hpp"

using namespace emerick::app;

namespace {

constexpr double kMaxLuaTimeoutMs = 86'400'000;
constexpr double kMaxLuaMemoryMb = 1 << 20;
constexpr double kBytesPerMb = 1 << 20;
constexpr double kMaxRenderSeconds = 86'400;

// Finder hands a double-clicked .erk bundle to the running app as a file open event.
class FileOpenFilter final : public QObject {
public:
    explicit FileOpenFilter(const Registry& registry) : registry_(registry) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        const auto* open = dynamic_cast<QFileOpenEvent*>(event);
        if (open == nullptr) {
            return QObject::eventFilter(watched, event);
        }
        try {
            registry_.run("project.open", {{"path", open->file().toStdString()}});
        } catch (const AppError& error) {
            qWarning("%s", error.what());
        }
        return true;
    }

private:
    const Registry& registry_;
};

// We read --headless, --script and --install-library before QGuiApplication exists, because Qt picks its platform
// plugin when it is built.
bool runsWithoutWindow(int argc, char** argv) {
    return std::ranges::any_of(std::span{argv, static_cast<std::size_t>(argc)}, [](const char* arg) {
        const std::string_view flag{arg};
        return flag == "--headless" || flag == "--script" || flag == "-s" || flag.starts_with("--script=") ||
               flag.starts_with("--install-library");
    });
}

int installLibraries(const Registry& registry, const QStringList& ids) {
    for (const QString& id : ids) {
        try {
            const Json installed = registry.run("library.install", {{"library", id.toStdString()}, {"wait", true}});
            std::cout << installed.at("name").get<std::string>() << " in " << installed.at("path").get<std::string>()
                      << '\n';
        } catch (const AppError& error) {
            qCritical("%s", error.what());
            return 1;
        }
    }
    return 0;
}

std::optional<double> positiveOption(const QCommandLineParser& parser, const QCommandLineOption& option,
                                     double maximum) {
    if (!parser.isSet(option)) {
        return std::nullopt;
    }
    bool valid = false;
    const double value = parser.value(option).toDouble(&valid);
    if (!valid || std::isnan(value) || value <= 0 || value > maximum) {
        throw std::invalid_argument(
            std::format("--{} expects a number above 0, up to {}", option.names().constLast().toStdString(), maximum));
    }
    return value;
}

int runWindow(Registry& registry, Nav& nav, ProjectController& controller, Console& console, ViewEngine& views,
              Monitors& monitors, Libraries& libraries) {
    TrackListModel tracks(controller, registry);
    FileOpenFilter fileOpen(registry);
    QGuiApplication::instance()->installEventFilter(&fileOpen);
    KeyFocusFilter keyFocus;
    QGuiApplication::instance()->installEventFilter(&keyFocus);
    PeakCache peaks;
    QQmlApplicationEngine engine;
    exposeToQml(engine, registry, nav, controller, tracks, peaks, &console, &monitors, &libraries);
    views.adopt(engine, peaks);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, QGuiApplication::instance(),
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Emerick.Ui", "Main");
    return QGuiApplication::exec();
}

int run(int argc, char** argv) {
#ifdef Q_OS_MACOS
    // A theme meant for Linux desktops, such as qt5ct, replaces the Cocoa theme, and with it the native file dialogs.
    qunsetenv("QT_QPA_PLATFORMTHEME");
#endif
    const bool headless = runsWithoutWindow(argc, argv);
    if (headless) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName("Emerick");
    QGuiApplication::setOrganizationName("Emerick");
    QGuiApplication::setApplicationVersion(appVersion());
    QGuiApplication::setWindowIcon(QIcon(":/icons/emerick.png"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption mcpPort("mcp-port",
                                     "Serve MCP on this port, 0 to disable (default: the mcp.port setting).", "port");
    const QCommandLineOption mcpBindAny("mcp-bind-any", "Accept MCP connections on every interface (for Docker).");
    const QCommandLineOption noLuaMcp("no-lua-mcp", "Leave Lua out of MCP: lua.eval and the actions that run Lua.");
    const QCommandLineOption audio("audio", "Audio backend: null or default.", "backend", "default");
    const QCommandLineOption midi("midi", "MIDI backend: virtual or default.", "backend", "default");
    const QCommandLineOption profile(
        "profile", "Keep settings, scripts and new projects in this folder (for test runs).", "folder");
    const QCommandLineOption agentReadRoot("agent-read-root",
                                           "Let MCP and Lua requests read files only inside this folder.", "folder");
    const QCommandLineOption agentWriteRoot("agent-write-root",
                                            "Let MCP and Lua requests write files only inside this folder.", "folder");
    const QCommandLineOption headlessOption("headless", "Run with no window, driven over MCP, which needs a port.");
    const QCommandLineOption scriptOption(
        {"s", "script"}, "Run this Lua script with no window and exit; pass its arguments after --.", "file");
    const QCommandLineOption luaTimeout(
        "lua-timeout-ms",
        "Stop a Lua script or eval after this many ms of wall time, counting the actions it runs and leaving out "
        "emerick.wait (default: no limit).",
        "ms");
    const QCommandLineOption luaMemory("lua-memory-mb", "Let Lua hold at most this many MB (default: 256).", "mb");
    const QCommandLineOption maxRender(
        "max-render-seconds", "Refuse an offline render longer than this much audio (default: no limit).", "seconds");
    const QCommandLineOption bundledPluginsOnly(
        "bundled-plugins-only", "Load and scan only the plugins the app ships, never system or user plugin folders.");
    const QCommandLineOption installLibrary(
        "install-library", "Install this sound library with no window and exit; repeat it for more libraries.", "id");
    parser.addOptions({headlessOption, scriptOption, mcpPort, mcpBindAny, noLuaMcp, audio, midi, profile, agentReadRoot,
                       agentWriteRoot, luaTimeout, luaMemory, maxRender, bundledPluginsOnly, installLibrary});
    parser.addPositionalArgument("args", "Arguments the script reads from its arg table.", "[-- args...]");
    parser.process(application);
    const bool script = parser.isSet(scriptOption);

    const QString profileFolder = parser.value(profile);
    Registry registry(profileFolder.isEmpty() ? QString{} : profileFolder + "/settings.ini");
    registry.setHeadless(headless);
    const auto optionalFolder = [&parser](const QCommandLineOption& option) {
        return parser.isSet(option) ? std::optional{std::filesystem::path{parser.value(option).toStdString()}}
                                    : std::nullopt;
    };
    registry.setAgentRoots({.read = optionalFolder(agentReadRoot), .write = optionalFolder(agentWriteRoot)});
    const std::optional<double> luaMemoryMb = positiveOption(parser, luaMemory, kMaxLuaMemoryMb);
    registry.setLimits(
        {.luaTimeout = positiveOption(parser, luaTimeout, kMaxLuaTimeoutMs).transform([](double ms) {
             return std::chrono::milliseconds{std::llround(ms)};
         }),
         .luaMemoryBytes = luaMemoryMb ? static_cast<std::size_t>(*luaMemoryMb * kBytesPerMb) : Limits{}.luaMemoryBytes,
         .renderSeconds = positiveOption(parser, maxRender, kMaxRenderSeconds),
         .bundledPluginsOnly = parser.isSet(bundledPluginsOnly),
         .libraryInstall =
             !parser.isSet(headlessOption) || qEnvironmentVariable("EMERICK_LIBRARY_INSTALL") == QStringLiteral("1")});
    const std::filesystem::path appFolder = QCoreApplication::applicationDirPath().toStdString();
    if (registry.limits().bundledPluginsOnly) {
        emerick::engine::allowPluginsOnlyIn(bundledClapFolder(appFolder));
    }
    Nav nav;
    const Backends backends{parser.value(audio).toStdString(), parser.value(midi).toStdString()};
    registerCoreActions(registry, nav, backends);
    auto midiBackend = makeMidiBackend(backends.midi);
    if (auto* virtualMidi = dynamic_cast<emerick::engine::VirtualMidiBackend*>(midiBackend.get())) {
        registerVirtualMidiActions(registry, *virtualMidi);
    }
    ProjectController controller(
        registry, makeAudioBackend(backends.audio), std::move(midiBackend),
        {.sampleRate = registry.settingValue("audio.sampleRate").get<double>(),
         .blockFrames = registry.settingValue("audio.bufferSize").get<emerick::core::Frames>(),
         .recordingDelay = registry.settingValue("audio.recordingDelay").get<emerick::core::Frames>()},
        profileFolder.isEmpty() ? std::filesystem::path{}
                                : std::filesystem::path{profileFolder.toStdString()} / "projects");
    ViewEngine views(registry, nav, controller);
    registerProjectActions(registry, controller, nav, views);
    PluginCatalog plugins(
        {.cacheFolder =
             (profileFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : profileFolder)
                 .toStdString(),
         .scanner = (QCoreApplication::applicationDirPath() + "/emerick-plugin-scanner").toStdString(),
         .folders = registry.limits().bundledPluginsOnly
                        ? std::vector{bundledClapFolder(appFolder)}
                        : pluginSearchFolders(appFolder, libraryFolder(profileFolder))});
    registerPluginActions(registry, nav, plugins);
    registerInstrumentActions(registry, controller, nav, plugins, libraryFolder(profileFolder));
    const auto yamaha = emerick::yamaha::registerWith(registry, controller, nav);
    const auto monitors = registerMonitors(registry, controller, nav);
    const auto libraries = registerLibraries(registry, nav, libraryFolder(profileFolder));
    if (parser.isSet(installLibrary)) {
        return installLibraries(registry, parser.values(installLibrary));
    }
    bool validPort = true;
    quint16 port = script ? 0 : registry.settingValue("mcp.port").get<quint16>();
    if (parser.isSet(mcpPort)) {
        port = parser.value(mcpPort).toUShort(&validPort);
    }
    if (!validPort) {
        qCritical("--mcp-port expects a number from 0 to 65535");
        return 2;
    }
    if (headless && !script && port == 0) {
        qCritical("--headless needs an MCP port: pass --mcp-port or set mcp.port");
        return 2;
    }

    const UiApi ui(registry);
    LuaHost lua(registry, ui);
    Console console(registry, lua);
    registerConsoleActions(registry, nav, console);
    const std::filesystem::path userScripts =
        (profileFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : profileFolder)
            .toStdString() +
        "/scripts";
    copyExampleScripts(userScripts);
    ScriptLibrary scripts(registry, console, userScripts);
    registerScriptActions(registry, scripts);
    const auto followProject = [&] { scripts.setProjectFolder(controller.folder() / "scripts"); };
    followProject();
    QObject::connect(&controller, &ProjectController::fileChanged, &scripts, followProject);
    McpServer mcp(registry, mcpTools(registry, ui, parser.isSet(noLuaMcp) ? nullptr : &lua, &console));
    if (port != 0) {
        const quint16 bound = mcp.listen(port, parser.isSet(mcpBindAny));
        qInfo("MCP on http://%s:%u/mcp", parser.isSet(mcpBindAny) ? "0.0.0.0" : "127.0.0.1", bound);
    }

    if (script) {
        QTimer::singleShot(0, &application, [&] {
            const auto args = parser.positionalArguments() |
                              std::views::transform([](const QString& arg) { return arg.toStdString(); }) |
                              std::ranges::to<std::vector>();
            QCoreApplication::exit(
                runCommandLineScript(lua, parser.value(scriptOption).toStdString(), args, std::cout, std::cerr));
        });
    }
    return headless ? QGuiApplication::exec()
                    : runWindow(registry, nav, controller, console, views, *monitors, *libraries);
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef __GLIBC__
    // glibc keeps what each thread frees in that thread's own heap, so every export of a sampled song grew a session
    // by about 0.5 GB; with one heap we reuse that memory.
    mallopt(M_ARENA_MAX, 1);
#endif
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
