#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

#include "emerick/app/qml_exposure.hpp"
#include "project_rig.hpp"

using namespace emerick;
using namespace emerick::app;
using namespace emerick::app::test;

namespace {

constexpr int kViewWidth = 1000;
constexpr int kViewHeight = 400;

// QML singletons are registered once per process, so every test shares one view over one project.
struct Timeline {
    ProjectRig rig;
    TrackListModel tracks{*rig.controller, rig.registry};
    PeakCache peaks;
    QQuickView view;

    Timeline() {
        exposeToQml(*view.engine(), rig.registry, rig.nav, *rig.controller, tracks, peaks);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.loadFromModule("Emerick.Ui", "TrackList");
        REQUIRE(view.status() == QQuickView::Ready);
        view.resize(kViewWidth, kViewHeight);
        view.show();
        REQUIRE(QTest::qWaitForWindowExposed(&view));
    }

    static Timeline& instance() {
        // We never delete the view, because destroying Qt Quick during static teardown crashes.
        static auto* timeline = new Timeline;
        return *timeline;
    }

    // Adds an audio track holding one clip of that many frames at bar 1 and returns the clip id.
    std::string addClip(core::Frames frames = 48000) {
        const std::string track = rig.run("track.addAudio").at("id");
        const std::string name = "ones-" + std::to_string(frames) + ".wav";
        writeConstant(rig.path(name), frames, 1.0F);
        std::string clip = rig.run("clip.import", {{"track", track}, {"path", rig.path(name).string()}}).at("id");
        settle();
        return clip;
    }

    // Lets the views drop the delegates an edit replaced, so a search never finds a stale clip.
    static void settle() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    [[nodiscard]] QQuickItem* item(const QString& objectName) const {
        QQuickItem* found = nullptr;
        processUntil([&] {
            found = find(view.rootObject(), objectName);
            return found != nullptr && found->isVisible();
        });
        REQUIRE(found != nullptr);
        return found;
    }

    [[nodiscard]] const model::AudioClip& clip(const std::string& id) const {
        return std::get<model::AudioClip>(*rig.controller->project().findClip(model::ClipId{id})->clip);
    }

    void send(QEvent::Type type, QPointF at, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event{type, at, view.mapToGlobal(at), button, buttons, modifiers};
        QCoreApplication::sendEvent(&view, &event);
    }

    // Presses at the center of the named item, moves by dx and dy in steps, and releases.
    void drag(const QString& objectName, double dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier, double dy = 0) {
        const QPointF from = press(objectName, modifiers);
        moveBy(from, {dx, dy}, modifiers);
        send(QEvent::MouseButtonRelease, from + QPointF{dx, dy}, Qt::NoButton, modifiers);
        settle();
    }

    QPointF press(const QString& objectName, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QQuickItem* target = item(objectName);
        const QPointF from = target->mapToScene(QPointF{target->width() / 2, target->height() / 2});
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, modifiers);
        return from;
    }

    void moveBy(QPointF from, QPointF by, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        constexpr int kSteps = 5;
        for (int step = 1; step <= kSteps; ++step) {
            send(QEvent::MouseMove, from + by * step / kSteps, Qt::LeftButton, modifiers);
        }
    }

    void wheel(QPoint angle, Qt::KeyboardModifiers modifiers) {
        const QPointF at{kViewWidth / 2.0, kViewHeight / 2.0};
        QWheelEvent event{at, view.mapToGlobal(at), {}, angle, Qt::NoButton, modifiers, Qt::NoScrollPhase, false};
        QCoreApplication::sendEvent(&view, &event);
    }

private:
    static QQuickItem* find(QQuickItem* root, const QString& objectName) {
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
};

}  // namespace

TEST_CASE("dragging a clip selects it and moves it to the nearest beat") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();
    const auto pixelsPerTick = timeline.rig.nav.pixelsPerTick();

    timeline.drag(QString::fromStdString("clip.move:" + id), 1000 * pixelsPerTick);
    CHECK(timeline.rig.nav.selectedClips() == QStringList{QString::fromStdString(id)});
    CHECK(timeline.clip(id).start == 960);
    CHECK(timeline.rig.controller->project().undoName() == "move clip");

    timeline.drag(QString::fromStdString("clip.move:" + id), 100 * pixelsPerTick, Qt::AltModifier);
    CHECK(timeline.clip(id).start == 1060);
}

TEST_CASE("dragging a fade handle inward sets the fade without snapping") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();
    timeline.rig.run("selection.set", {{"clips", {id}}});
    const auto pixelsPerTick = timeline.rig.nav.pixelsPerTick();

    timeline.drag(QString::fromStdString("clip.setFade:" + id + ":in"), 400 * pixelsPerTick);
    CHECK(timeline.clip(id).fadeIn == 400);
    CHECK(timeline.rig.controller->project().undoName() == "set clip fades");

    timeline.drag(QString::fromStdString("clip.setFade:" + id + ":out"), -300 * pixelsPerTick);
    CHECK(timeline.clip(id).fadeOut == 300);
    CHECK(timeline.clip(id).fadeIn == 400);
}

TEST_CASE("a ten minute clip draws only the pixel columns in view at any zoom and scroll") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip(48000 * 600);
    QQuickItem* tracks = timeline.item("tracks");
    const QString name = QString::fromStdString("waveform:" + id);
    const double laneWidth = tracks->property("laneWidth").toDouble();
    const auto columns = [&] { return timeline.item(name)->property("drawnColumns").toInt(); };

    CHECK(timeline.item(name)->width() > 20 * laneWidth);
    CHECK(columns() > 0);
    CHECK(columns() <= laneWidth + 1);
    tracks->setProperty("scrollTicks", 500000);
    CHECK(columns() > 0);
    CHECK(columns() <= laneWidth + 1);
    timeline.rig.nav.setPixelsPerTick(kMaxPixelsPerTick);
    CHECK(columns() <= laneWidth + 1);
    timeline.rig.nav.setPixelsPerTick(kMinPixelsPerTick);
    tracks->setProperty("scrollTicks", 0);
    CHECK(columns() <= laneWidth + 1);
    timeline.rig.nav.setPixelsPerTick(kDefaultPixelsPerTick);
}

TEST_CASE("dragging a clip edge trims it on the beat grid") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();

    timeline.drag(QString::fromStdString("clip.resize:" + id + ":right"), -900 * timeline.rig.nav.pixelsPerTick());
    CHECK(timeline.clip(id).length == 960);
}

namespace {

std::string trackOf(const Timeline& timeline, const std::string& clip) {
    return timeline.rig.controller->project().findClip(model::ClipId{clip})->track->id.value;
}

}  // namespace

TEST_CASE("dragging a clip down a row moves it to that audio track in one undo step") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();
    const std::string from = trackOf(timeline, id);
    const std::string to = timeline.rig.run("track.addAudio").at("id");
    Timeline::settle();

    timeline.drag(QString::fromStdString("clip.move:" + id), 0, Qt::NoModifier, timeline.rig.nav.trackHeight());
    CHECK(trackOf(timeline, id) == to);
    CHECK(timeline.clip(id).start == 0);
    CHECK(timeline.rig.controller->project().undoName() == "move clip");
    timeline.rig.run("project.undo");
    CHECK(trackOf(timeline, id) == from);
    Timeline::settle();
}

TEST_CASE("dragging an audio clip over a MIDI track marks the lane as refused and leaves the clip") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();
    const std::string from = trackOf(timeline, id);
    const std::string keys = timeline.rig.run("track.addMidi").at("id");
    Timeline::settle();

    const QPointF start = timeline.press(QString::fromStdString("clip.move:" + id));
    const QPointF down{0, static_cast<double>(timeline.rig.nav.trackHeight())};
    timeline.moveBy(start, down);
    QQuickItem* lane = timeline.item(QString::fromStdString("lane.drop:" + keys));
    CHECK(lane->isVisible());
    CHECK_FALSE(lane->property("accepts").toBool());
    timeline.send(QEvent::MouseButtonRelease, start + down, Qt::NoButton, Qt::NoModifier);
    Timeline::settle();
    CHECK(trackOf(timeline, id) == from);
}

TEST_CASE("dragging one of several selected clips moves the whole selection by the same rows and time") {
    Timeline& timeline = Timeline::instance();
    const std::string first = timeline.addClip();
    const std::string from = trackOf(timeline, first);
    const std::string second =
        timeline.rig
            .run("clip.import",
                 {{"track", from}, {"path", timeline.rig.path("ones-48000.wav").string()}, {"at", "3.1.0"}})
            .at("id");
    const std::string to = timeline.rig.run("track.addAudio").at("id");
    timeline.rig.run("selection.set", {{"clips", {first, second}}});
    Timeline::settle();
    const auto pixelsPerTick = timeline.rig.nav.pixelsPerTick();

    timeline.drag(QString::fromStdString("clip.move:" + first), 960 * pixelsPerTick, Qt::NoModifier,
                  timeline.rig.nav.trackHeight());
    CHECK(trackOf(timeline, first) == to);
    CHECK(trackOf(timeline, second) == to);
    CHECK(timeline.clip(first).start == 960);
    CHECK(timeline.clip(second).start == 2 * timeline.rig.controller->ticksPerBar() + 960);
    timeline.rig.run("project.undo");
    CHECK(trackOf(timeline, second) == from);
    Timeline::settle();
}

TEST_CASE("the wheel scrolls sideways with Shift or a horizontal swipe, zooms with Ctrl and sizes tracks with Alt") {
    Timeline& timeline = Timeline::instance();
    Nav& nav = timeline.rig.nav;
    nav.setScrollTicks(0);
    timeline.wheel({0, -120}, Qt::ShiftModifier);
    CHECK(nav.scrollTicks() > 0);
    const double scrolled = nav.scrollTicks();
    timeline.wheel({-120, 0}, Qt::NoModifier);
    CHECK(nav.scrollTicks() > scrolled);

    const double zoom = nav.pixelsPerTick();
    timeline.wheel({0, 120}, Qt::ControlModifier);
    CHECK(nav.pixelsPerTick() > zoom);

    const int height = nav.trackHeight();
    timeline.wheel({0, 120}, Qt::AltModifier);
    CHECK(nav.trackHeight() > height);
    timeline.wheel({0, -240}, Qt::ControlModifier | Qt::AltModifier);
    CHECK(nav.trackHeight() < height);
    CHECK(nav.pixelsPerTick() > zoom);
}
