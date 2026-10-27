#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtSystemDetection>
#include <exception>
#include <filesystem>
#include <optional>
#include <utility>

#include "emerick/app/backends.hpp"
#include "emerick/app/console.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/key_focus.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/mcp_server.hpp"
#include "emerick/app/mcp_tools.hpp"
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
#include "emerick/yamaha/plugin.hpp"

using namespace emerick::app;

namespace {

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

int run(int argc, char** argv) {
#ifdef Q_OS_MACOS
    // A theme meant for Linux desktops, such as qt5ct, replaces the Cocoa theme, and with it the native file dialogs.
    qunsetenv("QT_QPA_PLATFORMTHEME");
#endif
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
    parser.addOptions({mcpPort, mcpBindAny, noLuaMcp, audio, midi, profile, agentReadRoot, agentWriteRoot});
    parser.process(application);

    const QString profileFolder = parser.value(profile);
    Registry registry(profileFolder.isEmpty() ? QString{} : profileFolder + "/settings.ini");
    const auto optionalFolder = [&parser](const QCommandLineOption& option) {
        return parser.isSet(option) ? std::optional{std::filesystem::path{parser.value(option).toStdString()}}
                                    : std::nullopt;
    };
    registry.setAgentRoots({.read = optionalFolder(agentReadRoot), .write = optionalFolder(agentWriteRoot)});
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
    registerProjectActions(registry, controller, nav);
    PluginCatalog plugins(
        {.cacheFolder =
             (profileFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : profileFolder)
                 .toStdString(),
         .scanner = (QCoreApplication::applicationDirPath() + "/emerick-clap-scanner").toStdString(),
         .folders = clapSearchFolders(QCoreApplication::applicationDirPath().toStdString())});
    registerPluginActions(registry, nav, plugins);
    registerInstrumentActions(registry, controller, nav, plugins);
    const auto yamaha = emerick::yamaha::registerWith(registry, controller, nav);
    TrackListModel tracks(controller, registry);
    FileOpenFilter fileOpen(registry);
    QGuiApplication::instance()->installEventFilter(&fileOpen);
    KeyFocusFilter keyFocus;
    QGuiApplication::instance()->installEventFilter(&keyFocus);

    bool validPort = true;
    const auto port = parser.isSet(mcpPort) ? parser.value(mcpPort).toUShort(&validPort)
                                            : registry.settingValue("mcp.port").get<quint16>();
    if (!validPort) {
        qCritical("--mcp-port expects a number from 0 to 65535");
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

    PeakCache peaks;
    QQmlApplicationEngine engine;
    exposeToQml(engine, registry, nav, controller, tracks, peaks, &console);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Emerick.Ui", "Main");
    return QGuiApplication::exec();
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
