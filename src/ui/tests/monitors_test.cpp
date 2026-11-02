#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <vector>

#include "main_window.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

void playTone(const MainWindow& main, double hz) {
    constexpr std::size_t kFrames = 48000 * 4;
    std::vector<float> samples(kFrames);
    for (std::size_t i = 0; i < kFrames; ++i) {
        samples[i] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
    }
    main.rig.audio->setInput("interface", 1, std::move(samples));
}

QQuickWindow* monitorWindow(const MainWindow& main, const QString& objectName) {
    QQuickWindow* found = nullptr;
    processUntil([&] {
        found = main.window->findChild<QQuickWindow*>(objectName);
        return found != nullptr;
    });
    REQUIRE(found != nullptr);
    REQUIRE(QTest::qWaitForWindowExposed(found));
    return found;
}

QQuickItem* itemIn(QQuickWindow* window, const QString& objectName) {
    QQuickItem* found = MainWindow::find(window->contentItem(), objectName);
    REQUIRE(found != nullptr);
    return found;
}

}  // namespace

TEST_CASE("the tuner button opens a window whose needle and note settle on a tone and closes it again") {
    MainWindow& main = MainWindow::instance();
    const std::string guitar = main.rig.run("track.addAudio", {{"name", "Guitar"}}).at("id");
    main.rig.run("track.select", {{"track", guitar}});
    main.rig.run("track.setArm", {{"track", guitar}, {"value", true}});
    main.click(main.item("view.toggleTuner"));
    CHECK(main.rig.nav.tunerOpen());
    QQuickWindow* tuner = monitorWindow(main, "window.tuner");
    for (const char* name : {"tuner.setSource", "tuner.referencePitch", "tuner.gauge"}) {
        CHECK(itemIn(tuner, name) != nullptr);
    }

    playTone(main, 82.4069 * std::pow(2.0, -20.0 / 1200));
    QQuickItem* note = itemIn(tuner, "tuner.note");
    QQuickItem* needle = itemIn(tuner, "tuner.needle");
    CHECK(processUntil([&] {
        return note->property("text").toString() == "E" && std::abs(needle->rotation() + (20.0 / 50 * 60)) < 1.0;
    }));
    CHECK(main.rig.registry.state("tuner").at("note") == "E2");

    main.click(main.item("view.toggleTuner"));
    CHECK(processUntil([&] { return main.window->findChild<QQuickWindow*>("window.tuner") == nullptr; }));
    main.rig.run("track.setArm", {{"track", guitar}, {"value", false}});
    MainWindow::settle();
}

TEST_CASE("the spectrogram button opens a window that draws rows and reads out the frequency under the pointer") {
    MainWindow& main = MainWindow::instance();
    const std::string synth = main.rig.run("track.addAudio", {{"name", "Synth"}}).at("id");
    main.rig.run("track.setMonitor", {{"track", synth}, {"value", true}});
    main.click(main.item("view.toggleSpectrogram"));
    QQuickWindow* spectrogram = monitorWindow(main, "window.spectrogram");
    for (const char* name :
         {"spectrogram.setSource", "spectrogram.fftSize", "spectrogram.window", "spectrogram.frequencyAxis",
          "spectrogram.floorDb", "spectrogram.ceilingDb", "spectrogram.showSpectrum"}) {
        CHECK(itemIn(spectrogram, name) != nullptr);
    }

    playTone(main, 440.0);
    QQuickItem* view = itemIn(spectrogram, "spectrogram.view");
    CHECK(processUntil([&] { return view->property("rowsDrawn").toInt() > 10; }));
    QVariantMap reading;
    QMetaObject::invokeMethod(view, "readout", Q_RETURN_ARG(QVariantMap, reading), Q_ARG(double, view->width() / 2),
                              Q_ARG(double, view->property("waterfallTop").toDouble() + 1));
    CHECK(reading.value("hz").toDouble() > 20.0);
    CHECK(reading.contains("db"));
    CHECK(!view->property("frequencyTicks").toList().isEmpty());
    spectrogram->grabWindow();

    main.click(main.item("view.toggleSpectrogram"));
    CHECK_FALSE(main.rig.nav.spectrogramOpen());
    main.rig.run("track.setMonitor", {{"track", synth}, {"value", false}});
    MainWindow::settle();
}
