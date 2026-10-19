#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtLogging>
#include <exception>
#include <optional>
#include <utility>

#include "emerick/app/backends.hpp"
#include "emerick/app/core_actions.hpp"
#include "emerick/app/lua_host.hpp"
#include "emerick/app/mcp_server.hpp"
#include "emerick/app/mcp_tools.hpp"
#include "emerick/app/nav.hpp"
#include "emerick/app/peak_cache.hpp"
#include "emerick/app/project_actions.hpp"
#include "emerick/app/project_controller.hpp"
#include "emerick/app/qml_exposure.hpp"
#include "emerick/app/registry.hpp"
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
    const QCommandLineOption profile("profile", "Keep settings and new projects in this folder (for test runs).",
                                     "folder");
    parser.addOptions({mcpPort, mcpBindAny, noLuaMcp, audio, midi, profile});
    parser.process(application);

    const QString profileFolder = parser.value(profile);
    Registry registry(profileFolder.isEmpty() ? QString{} : profileFolder + "/settings.ini");
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
    emerick::yamaha::registerWith(registry, controller, nav);
    TrackListModel tracks(controller, registry);
    FileOpenFilter fileOpen(registry);
    QGuiApplication::instance()->installEventFilter(&fileOpen);

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

    PeakCache peaks;
    QQmlApplicationEngine engine;
    exposeToQml(engine, registry, nav, controller, tracks, peaks);
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
