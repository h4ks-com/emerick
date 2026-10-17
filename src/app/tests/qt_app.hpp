#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <functional>

// Catch2 owns main, so each Qt test creates the one application lazily.
inline void ensureApplication() {
    static int argc = 1;
    static char name[] = "emerick_app_tests";
    static char* argv[] = {name, nullptr};
    // We never delete the application, because destroying it during static teardown crashes Qt Quick.
    static const auto* application = new QGuiApplication(argc, argv);
    (void)application;
}

inline bool processUntil(const std::function<bool()>& done, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}
