#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQuickView>
#include <QTimer>
#include <QUrl>
#include <exception>

#include "viewer.hpp"

#ifdef Q_OS_WASM
#include <emscripten/val.h>

#include <string>
#endif

using emerick::viewer::Viewer;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

#ifdef Q_OS_WASM
constexpr int kAudioPollMs = 30;

emscripten::val mixElement() {
    return emscripten::val::global("document").call<emscripten::val>("getElementById", std::string{"mix"});
}

// The page's <audio id="mix"> plays the mix the service renders; its time moves the playhead.
void followMix(Viewer& viewer, QObject* owner) {
    auto* timer = new QTimer(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [&viewer] {
        if (const emscripten::val mix = mixElement(); !mix.isNull()) {
            viewer.project.setPlaying(!mix["paused"].as<bool>());
            viewer.project.setPositionSeconds(mix["currentTime"].as<double>());
        }
    });
    timer->start(kAudioPollMs);
    QObject::connect(&viewer.registry, &emerick::viewer::ViewerRegistry::seekRequested, owner, [](double at) {
        if (emscripten::val mix = mixElement(); !mix.isNull()) {
            mix.set("currentTime", at);
        }
    });
    QObject::connect(&viewer.registry, &emerick::viewer::ViewerRegistry::playToggled, owner, [] {
        if (emscripten::val mix = mixElement(); !mix.isNull()) {
            mix.call<void>(mix["paused"].as<bool>() ? "play" : "pause");
        }
    });
}
#endif

void loadProject(Viewer& viewer, QNetworkAccessManager& network, const QUrl& url) {
    QNetworkReply* reply = network.get(QNetworkRequest{url});
    QObject::connect(reply, &QNetworkReply::finished, &viewer.project, [&viewer, reply] {
        const QString error =
            reply->error() == QNetworkReply::NoError
                ? viewer.load(reply->readAll(),
                              reply->url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).fileName())
                : reply->errorString();
        if (!error.isEmpty()) {
            qWarning().noquote() << error;
        }
        reply->deleteLater();
    });
}

int run(int argc, char** argv) {
    const QGuiApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addPositionalArgument("project", "URL or path of the project.json to show.");
    const QCommandLineOption bundle("bundle", "URL of the bundle folder the peak files sit in.", "url");
    parser.addOption(bundle);
    parser.process(application);

    const QUrl project = QUrl::fromUserInput(parser.positionalArguments().value(0, QStringLiteral("project.json")),
                                             QDir::currentPath(), QUrl::AssumeLocalFile);
    Viewer viewer;
    viewer.peaks.setBundle(parser.isSet(bundle) ? QUrl{parser.value(bundle)} : project.adjusted(QUrl::RemoveFilename));
    viewer.exposeToQml();

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.loadFromModule("Emerick.Viewer", "Viewer");
    if (view.status() == QQuickView::Error) {
        return 1;
    }
    view.resize(kWidth, kHeight);
#ifdef Q_OS_WASM
    followMix(viewer, &view);
#endif
    QNetworkAccessManager network;
    loadProject(viewer, network, project);
    view.show();
    return QGuiApplication::exec();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
