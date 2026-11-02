#include "viewer.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <vector>

#include "emerick/app/peak_levels.hpp"
#include "emerick/model/json.hpp"
#include "qt_app.hpp"

using namespace emerick;

namespace {

constexpr int kWidth = 1000;
constexpr int kHeight = 400;
constexpr core::Ticks kBar = 4 * core::kTicksPerQuarter;
constexpr std::size_t kPeaks = 750;
constexpr float kPeak = 0.5F;

model::Document sampleDocument() {
    model::Document document;
    model::Track keys{.name = "Keys", .kind = "midi", .color = 2};
    model::MidiClip phrase{.start = kBar, .length = 2 * kBar, .name = "Phrase"};
    for (int step = 0; step < 8; ++step) {
        phrase.notes.push_back(
            model::Note{.pitch = 60 + step, .start = step * core::kTicksPerQuarter, .length = core::kTicksPerQuarter});
    }
    keys.clips.emplace_back(phrase);
    document.tracks.push_back(keys);
    model::Track voice{.name = "Voice", .kind = "audio", .color = 4};
    voice.clips.emplace_back(model::AudioClip{.start = 0, .length = 2 * kBar, .file = "audio/voice.wav"});
    voice.automation.push_back(model::AutomationLane{
        .param = "gain", .points = {{.tick = 0, .value = -12.0}, {.tick = 3 * kBar, .value = 0.0}}});
    document.tracks.push_back(voice);
    document.markers.push_back(model::Marker{.name = "Verse", .at = kBar});
    return document;
}

void writeBundle(const QTemporaryDir& bundle) {
    QDir{bundle.path()}.mkpath("audio/.peaks");
    QFile file{bundle.filePath("audio/.peaks/voice.wav.peaks")};
    REQUIRE(file.open(QIODevice::WriteOnly));
    app::writePeakFile(file, {.stamp = {}, .peaks = {std::vector(kPeaks, -kPeak), std::vector(kPeaks, kPeak)}});
}

QQuickItem* find(QQuickItem* root, const QString& objectName) {
    if (root->objectName() == objectName) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = find(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

QString idOf(const model::Clip& clip) { return QString::fromStdString(model::clipId(clip).value); }

}  // namespace

TEST_CASE("the viewer draws a project with the app's tracks view and nothing in it edits") {
    ensureApplication();
    const QTemporaryDir bundle;
    writeBundle(bundle);
    // The view and the singletons it binds to live as long as the process, because Qt Quick crashes when destroyed
    // during static teardown.
    auto* viewer = new viewer::Viewer;
    viewer->exposeToQml();
    viewer->peaks.setBundle(QUrl::fromLocalFile(bundle.path()));
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(sampleDocument()).dump()), "Sample").isEmpty());
    viewer->nav.setAutomationOpen(true);
    auto* view = new QQuickView;
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->loadFromModule("Emerick.Viewer", "Viewer");
    REQUIRE(view->status() == QQuickView::Ready);
    view->resize(kWidth, kHeight);
    view->show();
    REQUIRE(QTest::qWaitForWindowExposed(view));

    const model::Document& document = viewer->project.document();
    const QString phrase = idOf(document.tracks[0].clips[0]);
    const QString voice = idOf(document.tracks[1].clips[0]);
    const QString voiceTrack = QString::fromStdString(document.tracks[1].id.value);
    QQuickItem* waveform = nullptr;
    REQUIRE(processUntil([&] {
        waveform = find(view->rootObject(), "waveform:" + voice);
        return waveform != nullptr && viewer->peaks.levels("audio/voice.wav") != nullptr;
    }));
    QQuickItem* midiClip = find(view->rootObject(), "clip:" + phrase);
    REQUIRE(midiClip != nullptr);
    QQuickItem* lane = find(view->rootObject(), "automation.lane:" + voiceTrack);
    REQUIRE(lane != nullptr);
    CHECK(lane->isVisible());
    QTest::qWait(50);

    const QImage frame = view->grabWindow();
    const QPoint notes = midiClip->mapToScene({midiClip->width() / 2, midiClip->height() - 4}).toPoint();
    CHECK(frame.pixelColor(notes) != frame.pixelColor(kWidth - 5, notes.y()));
    const QPointF wave = waveform->mapToScene({waveform->width() / 4, 0});
    const QPoint middle{static_cast<int>(wave.x()), static_cast<int>(wave.y() + waveform->height() / 2)};
    const QPoint edge{static_cast<int>(wave.x()), static_cast<int>(wave.y() + waveform->height() / 10)};
    CHECK(frame.pixelColor(middle) != frame.pixelColor(edge));
    if (const QByteArray path = qgetenv("EMERICK_VIEWER_GRAB"); !path.isEmpty()) {
        frame.save(QString::fromUtf8(path));
    }

    CHECK_FALSE(find(view->rootObject(), "clip.resize:" + phrase + ":left")->isVisible());
    CHECK_FALSE(find(view->rootObject(), "clip.move:" + phrase)->property("enabled").toBool());
    CHECK_FALSE(find(view->rootObject(), "track.setMute:" + voiceTrack)->isEnabled());
    CHECK(find(view->rootObject(), "track.setArm:" + voiceTrack)->opacity() == 0);
    CHECK(find(view->rootObject(), "transport.togglePlay")->isVisible());
    CHECK_FALSE(find(view->rootObject(), "transport.record")->isVisible());

    viewer->registry.runAction("transport.locate", {{"at", 2 * kBar}});
    CHECK(viewer->project.positionTicks() == 2 * kBar);
    CHECK(viewer->clock.bar() == 3);
    viewer->registry.activate("transport.gotoStart");
    CHECK(viewer->project.positionTicks() == 0);
    viewer->registry.runAction("transport.locate", {{"at", 2 * kBar}});
    viewer->registry.runAction("marker.previous", {});
    CHECK(viewer->project.positionTicks() == kBar);
    viewer->registry.activate("view.toggleAutomation");
    CHECK_FALSE(viewer->nav.automationOpen());
    viewer->registry.activate("transport.barForward");
    CHECK(viewer->project.positionTicks() == 2 * kBar);

    const double zoom = viewer->nav.pixelsPerTick();
    QTest::keyClick(view, Qt::Key_Right, Qt::ControlModifier);
    CHECK(viewer->nav.pixelsPerTick() > zoom);
    QTest::keyClick(view, Qt::Key_Return);
    CHECK(viewer->project.positionTicks() == 0);
    const QVariantList shortcuts = viewer->registry.shortcuts();
    CHECK(std::ranges::any_of(shortcuts, [](const QVariant& entry) { return entry.toMap()["shortcut"] == "Home"; }));
}
