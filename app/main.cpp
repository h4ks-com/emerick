#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtLogging>
#include <exception>
#include <optional>

#include "emerick/app/backends.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/mcp_server.hpp"
#include "emerick/app/mcp_tools.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/registry.hpp"
#include "emerick/app/track_list_model.hpp"
#include "emerick/app/ui_api.hpp"

using namespace emerick::app;

namespace {

int run(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName("Emerick");
    QGuiApplication::setOrganizationName("Emerick");
    QGuiApplication::setApplicationVersion(appVersion());

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption mcpPort("mcp-port",
                                     "Serve MCP on this port, 0 to disable (default: the mcp.port setting).", "port");
    const QCommandLineOption mcpBindAny("mcp-bind-any", "Accept MCP connections on every interface (for Docker).");
    const QCommandLineOption noLuaMcp("no-lua-mcp", "Leave the lua.eval tool out of MCP.");
    const QCommandLineOption audio("audio", "Audio backend: null or default.", "backend", "default");
    const QCommandLineOption midi("midi", "MIDI backend: virtual or default.", "backend", "default");
    parser.addOptions({mcpPort, mcpBindAny, noLuaMcp, audio, midi});
    parser.process(application);

    Registry registry;
    Nav nav;
    const Backends backends{parser.value(audio).toStdString(), parser.value(midi).toStdString()};
    registerCoreActions(registry, nav, backends);
    ProjectController controller(
        registry, makeAudioBackend(backends.audio), makeMidiBackend(backends.midi),
        {.sampleRate = 48000.0, .blockFrames = registry.settingValue("audio.bufferSize").get<emerick::core::Frames>()});
    registerProjectActions(registry, controller, nav);
    TrackListModel tracks(controller.project(), registry);

    bool validPort = true;
    const auto port = parser.isSet(mcpPort) ? parser.value(mcpPort).toUShort(&validPort)
                                            : registry.settingValue("mcp.port").get<quint16>();
    if (!validPort) {
        qCritical("--mcp-port expects a number from 0 to 65535");
        return 2;
    }

    const UiApi ui(registry);
    std::optional<LuaHost> lua;
    if (!parser.isSet(noLuaMcp)) {
        lua.emplace(registry, ui);
    }
    McpServer mcp(registry, mcpTools(registry, ui, lua ? &*lua : nullptr));
    if (port != 0) {
        const quint16 bound = mcp.listen(port, parser.isSet(mcpBindAny));
        qInfo("MCP on http://%s:%u/mcp", parser.isSet(mcpBindAny) ? "0.0.0.0" : "127.0.0.1", bound);
    }

    qmlRegisterUncreatableMetaObject(emerick::app::staticMetaObject, "Emerick.App", 1, 0, "App",
                                     "App only holds enums");
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Registry", &registry);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Project", &controller);
    qmlRegisterSingletonInstance("Emerick.App", 1, 0, "Tracks", &tracks);

    QQmlApplicationEngine engine;
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
