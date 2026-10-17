#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>
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

    // Adds an audio track holding one two-beat clip at bar 1 and returns the clip id.
    std::string addClip() {
        const std::string track = rig.run("track.addAudio").at("id");
        writeConstant(rig.path("ones.wav"), 48000, 1.0F);
        std::string clip = rig.run("clip.import", {{"track", track}, {"path", rig.path("ones.wav").string()}}).at("id");
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

    // Presses at the center of the named item, moves right by dx in steps, and releases.
    void drag(const QString& objectName, double dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QQuickItem* target = item(objectName);
        const QPointF from = target->mapToScene(QPointF{target->width() / 2, target->height() / 2});
        constexpr int kSteps = 5;
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, modifiers);
        for (int step = 1; step <= kSteps; ++step) {
            send(QEvent::MouseMove, from + QPointF{dx * step / kSteps, 0}, Qt::LeftButton, modifiers);
        }
        send(QEvent::MouseButtonRelease, from + QPointF{dx, 0}, Qt::NoButton, modifiers);
        settle();
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

TEST_CASE("dragging a clip edge trims it on the beat grid") {
    Timeline& timeline = Timeline::instance();
    const std::string id = timeline.addClip();

    timeline.drag(QString::fromStdString("clip.resize:" + id + ":right"), -900 * timeline.rig.nav.pixelsPerTick());
    CHECK(timeline.clip(id).length == 960);
}
