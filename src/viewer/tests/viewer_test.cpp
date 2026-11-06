#include "viewer.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "emerick/app/peak_levels.hpp"
#include "emerick/model/json.hpp"
#include "qt_app.hpp"

using namespace emerick;

namespace {

constexpr int kWidth = 1000;
constexpr int kHeight = 700;
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
    phrase.events.push_back(model::MidiEvent{.at = core::kTicksPerQuarter, .bytes = {0xE0, 0x00, 0x50}});
    keys.clips.emplace_back(phrase);
    const std::string gmState = R"({"sound": 0})";
    keys.instrument = model::PluginSlot{
        .pluginId = "org.emerick.gm", .name = "General MIDI", .state = {gmState.begin(), gmState.end()}};
    document.tracks.push_back(keys);
    model::Track room{.name = "Room", .kind = "bus", .color = 6};
    model::Track voice{.name = "Voice", .kind = "audio", .color = 4};
    voice.clips.emplace_back(model::AudioClip{.start = 0, .length = 2 * kBar, .file = "audio/voice.wav"});
    voice.automation.push_back(model::AutomationLane{
        .param = "gain", .points = {{.tick = 0, .value = -12.0}, {.tick = 3 * kBar, .value = 0.0}}});
    voice.inserts.push_back(model::Insert{.type = "compressor"});
    voice.sends.push_back(model::Send{.bus = room.id, .levelDb = -6.0});
    document.tracks.push_back(voice);
    document.tracks.push_back(room);
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

struct ShownViewer {
    viewer::Viewer* viewer;
    QQuickView* view;
};

// The view and the singletons it binds to live as long as the process, because Qt Quick crashes when destroyed during
// static teardown, and the singletons register once, so every test shares one view.
ShownViewer shownViewer() {
    static const ShownViewer shown = [] {
        auto* viewer = new viewer::Viewer;
        viewer->exposeToQml();
        auto* view = new QQuickView;
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->loadFromModule("Emerick.Viewer", "Viewer");
        view->resize(kWidth, kHeight);
        view->show();
        return ShownViewer{.viewer = viewer, .view = view};
    }();
    return shown;
}

}  // namespace

TEST_CASE("the viewer draws a project with the app's tracks view and nothing in it edits") {
    ensureApplication();
    const QTemporaryDir bundle;
    writeBundle(bundle);
    const auto [viewer, view] = shownViewer();
    REQUIRE(view->status() == QQuickView::Ready);
    REQUIRE(QTest::qWaitForWindowExposed(view));
    viewer->peaks.setBundle(QUrl::fromLocalFile(bundle.path()));
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(sampleDocument()).dump()), "Sample").isEmpty());
    viewer->nav.setAutomationOpen(true);

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

    viewer->registry.runAction("transport.locate", {{"at", QVariant::fromValue(2 * kBar)}});
    CHECK(viewer->project.positionTicks() == 2 * kBar);
    CHECK(viewer->clock.bar() == 3);
    viewer->registry.activate("transport.gotoStart");
    CHECK(viewer->project.positionTicks() == 0);
    viewer->registry.runAction("transport.locate", {{"at", QVariant::fromValue(2 * kBar)}});
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

    viewer->registry.runAction("midi.editClip", {{"clip", phrase}});
    CHECK(viewer->nav.pianoRollOpen());
    CHECK(viewer->registry.settings()["midi.lanes"].toStringList().contains("pitchBend"));
    const QString firstNote =
        QString::fromStdString(std::get<model::MidiClip>(document.tracks[0].clips[0]).notes[0].id.value);
    REQUIRE(processUntil([&] { return find(view->rootObject(), "note:" + firstNote) != nullptr; }));
    CHECK_FALSE(find(view->rootObject(), "midi.addNote")->property("enabled").toBool());

    QTest::keyClick(view, Qt::Key_X);
    QQuickItem* instrument = nullptr;
    REQUIRE(processUntil([&] {
        instrument =
            find(view->rootObject(), "mixer.instrument:" + QString::fromStdString(document.tracks[0].id.value));
        return instrument != nullptr;
    }));
    CHECK(instrument->property("text").toString() == "Acoustic Grand Piano");
    CHECK(find(view->rootObject(), "insert.edit:" + QString::fromStdString(document.tracks[1].inserts[0].id.value)) !=
          nullptr);
    CHECK_FALSE(find(view->rootObject(), "insert.add:" + voiceTrack)->isVisible());
    if (const QByteArray path = qgetenv("EMERICK_VIEWER_GRAB_DOCKS"); !path.isEmpty()) {
        view->grabWindow().save(QString::fromUtf8(path));
    }
}

TEST_CASE("the viewer fetches a peak file again until the service has written it") {
    ensureApplication();
    const QTemporaryDir bundle;
    viewer::RemotePeaks peaks;
    peaks.setBundle(QUrl::fromLocalFile(bundle.path()));
    REQUIRE(peaks.levels("audio/voice.wav") == nullptr);
    QTest::qWait(500);
    REQUIRE(peaks.levels("audio/voice.wav") == nullptr);

    writeBundle(bundle);
    REQUIRE(processUntil(
        [&] {
            const app::PeakLevels* levels = peaks.levels("audio/voice.wav");
            return levels != nullptr && levels->levels.front().maximum.size() == kPeaks;
        },
        5000));
}

TEST_CASE("the viewer opens a project with the view it was saved with") {
    ensureApplication();
    model::Document document = sampleDocument();
    const std::string keys = document.tracks[0].id.value;
    document.view = {{"pixelsPerTick", 0.2},
                     {"trackHeight", 120},
                     {"automationOpen", false},
                     {"automationLanes", {{keys, nlohmann::json::array({"pan"})}}}};
    viewer::Viewer viewer;
    REQUIRE(viewer.load(QByteArray::fromStdString(model::toJson(document).dump()), "Sample").isEmpty());
    CHECK(viewer.nav.pixelsPerTick() == Catch::Approx(0.2));
    CHECK(viewer.nav.trackHeight() == 120);
    CHECK_FALSE(viewer.nav.automationOpen());
    CHECK(viewer.nav.automationLane(QString::fromStdString(keys)) == "pan");
}

TEST_CASE("the viewer opens a saved view with two lanes on a track and shows a row for each") {
    ensureApplication();
    const auto [viewer, view] = shownViewer();
    REQUIRE(QTest::qWaitForWindowExposed(view));
    model::Document document = sampleDocument();
    const QString voice = QString::fromStdString(document.tracks[1].id.value);
    document.view = {{"automationOpen", true}, {"automationLanes", {{voice.toStdString(), {"gain", "pan"}}}}};
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(document).dump()), "Sample").isEmpty());
    QQuickItem* pan = nullptr;
    REQUIRE(processUntil([&] {
        pan = find(view->rootObject(), "automation.lane:" + voice + ":pan");
        return pan != nullptr && pan->isVisible();
    }));
    QQuickItem* gain = find(view->rootObject(), "automation.lane:" + voice);
    REQUIRE(gain != nullptr);
    CHECK(gain->isVisible());
    CHECK(pan->mapToScene({0, 0}).y() == Catch::Approx(gain->mapToScene({0, gain->height()}).y()));

    viewer->registry.runAction("automation.hideLane", {{"track", voice}, {"param", "gain"}});
    CHECK(viewer->nav.shownLanes(voice) == QStringList{"pan"});
    viewer->registry.runAction("automation.showLane", {{"track", voice}, {"param", "mute"}, {"add", true}});
    CHECK(viewer->nav.shownLanes(voice) == QStringList{"pan", "mute"});
    // We delete the dropped lane row now, because Qt crashes when it does so at process exit.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("hovering a track header button in the viewer shows what it does") {
    ensureApplication();
    const auto [viewer, view] = shownViewer();
    REQUIRE(QTest::qWaitForWindowExposed(view));
    const model::Document document = sampleDocument();
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(document).dump()), "Sample").isEmpty());
    const QString voice = QString::fromStdString(document.tracks[1].id.value);
    const QString keys = QString::fromStdString(document.tracks[0].id.value);
    for (const QString& name : {"track.setMute:" + voice, "automation.setMode:" + voice, "track.setGain:" + keys,
                                "track.setGain:" + keys + ":db"}) {
        QQuickItem* button = nullptr;
        REQUIRE(processUntil([&] {
            button = find(view->rootObject(), name);
            return button != nullptr && button->isVisible();
        }));
        QTest::mouseMove(view, button->mapToScene({button->width() / 2, button->height() / 2}).toPoint());
        const auto tip = [&](const char* property) {
            return QQmlProperty(button, property, qmlContext(button)).read();
        };
        INFO(name.toStdString());
        CHECK(processUntil([&] { return tip("ToolTip.visible").toBool(); }));
        CHECK_FALSE(tip("ToolTip.text").toString().isEmpty());
        QTest::mouseMove(view, {1, 1});
    }
}

TEST_CASE("every control the viewer shows, with the mixer and automation open, names what it does") {
    ensureApplication();
    const auto [viewer, view] = shownViewer();
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(sampleDocument()).dump()), "Sample").isEmpty());
    viewer->nav.setMixerOpen(true);
    viewer->nav.setAutomationOpen(true);
    REQUIRE(processUntil([&] { return find(view->rootObject(), "mixer") != nullptr; }));
    QStringList untitled;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (item->isVisible() && item->opacity() > 0 && item->property("actionId").isValid() &&
            QQmlProperty(item, "ToolTip.text", qmlContext(item)).read().toString().isEmpty()) {
            untitled.append(item->property("actionId").toString());
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    walk(view->rootObject());
    CHECK(untitled.join(", ").toStdString().empty());
    viewer->nav.setMixerOpen(false);
    // We delete the closed mixer now, because Qt crashes when it does so at process exit.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("the viewer draws a Chords track's chord names and a Style track's sections as blocks") {
    ensureApplication();
    const auto [viewer, view] = shownViewer();
    model::Document document;
    model::Track chords{.name = "Chords", .kind = "yamaha.chord"};
    chords.data = nlohmann::json::parse(R"([{"at":0,"chord":"C"},{"at":1920,"chord":"G7"}])");
    model::Track style{.name = "Style", .kind = "yamaha.style"};
    style.data =
        nlohmann::json::parse(R"([{"at":0,"kind":"intro","variation":0},{"at":1920,"kind":"main","variation":1}])");
    document.tracks = {chords, style};
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(document).dump()), "Arranger").isEmpty());

    const auto label = [&](const model::Track& track, core::Ticks at) -> QString {
        QQuickItem* item =
            find(view->rootObject(), QString::fromStdString(std::format("lane.blockLabel:{}:{}", track.id.value, at)));
        return item != nullptr ? item->property("text").toString() : QString{};
    };
    REQUIRE(processUntil([&] { return label(chords, 0) == "C"; }));
    CHECK(label(chords, 1920) == "G7");
    CHECK(label(style, 0) == "Intro 1");
    CHECK(label(style, 1920) == "Main B");
}

TEST_CASE("the viewer shows the fills and breaks a style plays and names the voice of each part the style plays live") {
    ensureApplication();
    const auto [viewer, view] = shownViewer();
    const auto bytes = [](const std::string& text) { return std::vector<std::uint8_t>{text.begin(), text.end()}; };
    model::Document document;
    model::Track style{.name = "Style", .kind = "yamaha.style"};
    style.instrument = model::PluginSlot{.pluginId = "org.emerick.arranger",
                                         .name = "Arranger",
                                         .state = bytes(R"({"style":"/styles/Pop Ballad.sty"})")};
    style.data = nlohmann::json::parse(R"([{"at":0,"kind":"main","variation":0},
        {"at":3840,"kind":"main","variation":1},{"at":9600,"kind":"break","variation":0},
        {"at":13440,"kind":"ending","variation":0}])");
    model::Track bass{.name = "Bass", .kind = "midi"};
    bass.midiIn = {.channel = 11, .sourceTrack = style.id};
    bass.instrument =
        model::PluginSlot{.pluginId = "org.emerick.voices",
                          .name = "Expansion Voices",
                          .state = bytes(R"({"voice":{"name":"Fingered Bass","msb":0,"lsb":0,"program":33}})")};
    model::Track drums{.name = "Rhythm 1", .kind = "midi"};
    drums.midiIn = {.channel = 9, .sourceTrack = style.id};
    drums.instrument =
        model::PluginSlot{.pluginId = "org.emerick.gm", .name = "General MIDI", .state = bytes(R"({"drumKit":1})")};
    document.tracks = {style, bass, drums};
    REQUIRE(viewer->load(QByteArray::fromStdString(model::toJson(document).dump()), "Style parts").isEmpty());

    const auto label = [&](const model::Track& track, core::Ticks at) -> QString {
        QQuickItem* item =
            find(view->rootObject(), QString::fromStdString(std::format("lane.blockLabel:{}:{}", track.id.value, at)));
        return item != nullptr ? item->property("text").toString() : QString{};
    };
    REQUIRE(processUntil([&] { return label(style, 0) == "Main A"; }));
    CHECK(label(style, 3840) == "Main B (fill)");
    CHECK(label(style, 7680) == "Main B");
    CHECK(label(style, 9600) == "Break");
    CHECK(label(style, 11520) == "Main B");
    CHECK(label(style, 13440) == "Ending 1");
    CHECK(label(bass, 0) == "Bass: Fingered Bass played by the Pop Ballad style");
    CHECK(label(drums, 0) == "Rhythm 1: Room kit played by the Pop Ballad style");
}
