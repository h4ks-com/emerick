#include "emerick/app/project_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStringList>
#include <QtLogging>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "emerick/app/event_wait.hpp"
#include "emerick/app/project_bundle.hpp"
#include "emerick/app/project_state.hpp"
#include "emerick/app/session_mapping.hpp"
#include "emerick/app/stem_export.hpp"
#include "emerick/model/json.hpp"

namespace emerick::app {

namespace {

constexpr auto kPollPeriod = std::chrono::milliseconds{30};
// After the user leaves the app we keep the devices open this long, so switching windows briefly costs no restart.
constexpr auto kWarmAfterLeaving = std::chrono::seconds{20};
constexpr auto kTransportTimeout = std::chrono::milliseconds{500};
constexpr auto kTakeTimeout = std::chrono::seconds{3};
// The take writer polls every 5 ms, so a MIDI take that has events arrives well within this.
constexpr auto kTakeSettle = std::chrono::milliseconds{50};
constexpr double kLocateWindowSeconds = 0.25;
constexpr auto kAutosaveDelay = std::chrono::seconds{2};
constexpr double kPercent = 100.0;

std::string clippedText(const engine::RecordedTake& take) {
    const double share = kPercent * static_cast<double>(take.clippedSamples) /
                         static_cast<double>(std::max<std::uint64_t>(take.samples, 1));
    return std::format(
        "the input reached full scale on {} samples ({:.2f}%), so it clipped before Emerick; turn "
        "down the source's output level and record again",
        take.clippedSamples, share);
}

}  // namespace

ProjectController::ProjectController(const Registry& registry, std::unique_ptr<engine::AudioBackend> audio,
                                     std::unique_ptr<engine::MidiBackend> midi, engine::Engine::Options options,
                                     const std::filesystem::path& projectsFolder, QObject* parent)
    : QObject(parent),
      registry_(registry),
      options_(options),
      engine_(std::move(audio), std::move(midi), options),
      projectsFolder_(normalizedPath(projectsFolder.empty() ? defaultProjectsFolder() : projectsFolder)) {
    project_.addListener([this](const model::Change&) {
        setEdited(true);
        scheduleFlush();
    });
    autosaveTimer_.setSingleShot(true);
    autosaveTimer_.setInterval(kAutosaveDelay);
    connect(&autosaveTimer_, &QTimer::timeout, this, &ProjectController::autosave);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        try {
            leave();
        } catch (const AppError& error) {
            qWarning("cannot save %s: %s", folder_.c_str(), error.what());
        }
    });
    createUntitled();
    publish();
    connect(&timer_, &QTimer::timeout, this, &ProjectController::poll);
    timer_.start(kPollPeriod);
}

void ProjectController::discardIfEmpty() {
    if (untitled_ && holdsNothing(project_.document(), folder_)) {
        std::error_code error;
        std::filesystem::remove_all(folder_, error);
    }
}

QString ProjectController::name() const { return QString::fromStdString(folder_.stem().string()); }

void ProjectController::moveTo(const std::filesystem::path& folder, model::Document document) {
    try {
        std::filesystem::create_directories(folder / "audio");
    } catch (const std::filesystem::filesystem_error& error) {
        throw AppError(error.what());
    }
    folder_ = folder;
    untitled_ = false;
    takeErrors_.clear();
    project_.reset(std::move(document));
    setEdited(false);
    emit fileChanged();
}

void ProjectController::createUntitled() {
    moveTo(nextUntitledBundle(projectsFolder_), {});
    untitled_ = true;
    write();
}

void ProjectController::write() {
    try {
        model::saveProject(project_.document(), folder_);
    } catch (const std::runtime_error& error) {
        throw AppError(error.what());
    }
    setEdited(false);
}

void ProjectController::autosave() {
    try {
        write();
    } catch (const AppError& error) {
        qWarning("cannot autosave: %s", error.what());
    }
}

void ProjectController::setEdited(bool value) {
    if (value) {
        autosaveTimer_.start();
    } else {
        autosaveTimer_.stop();
    }
    if (std::exchange(edited_, value) != value) {
        emit fileChanged();
    }
}

void ProjectController::leave() {
    settleTakes();
    if (edited_) {
        write();
    }
    discardIfEmpty();
}

void ProjectController::settleTakes() {
    const bool wasRecording = recording_;
    if (recording_ || engine_.playing()) {
        stop();
    }
    if (wasRecording) {
        const auto settled = std::chrono::steady_clock::now() + kTakeSettle;
        const bool complete = processEventsUntil(
            [&] {
                collectTakes();
                return pendingAudioTakes_.empty() && std::chrono::steady_clock::now() >= settled;
            },
            kTakeTimeout);
        if (!complete) {
            qWarning("%zu audio takes did not finish in time", pendingAudioTakes_.size());
        }
    }
    pendingAudioTakes_.clear();
    collectTakes();
}

void ProjectController::newProject() {
    leave();
    createUntitled();
}

void ProjectController::open(const std::filesystem::path& path) {
    const std::filesystem::path bundle = bundleOf(path);
    if (bundle == folder_) {
        save();
        return;
    }
    model::Document document;
    try {
        document = model::loadProject(bundle);
    } catch (const std::runtime_error& error) {
        throw AppError(error.what());
    } catch (const nlohmann::json::exception& error) {
        throw AppError("cannot read " + (bundle / "project.json").string() + ": " + error.what());
    }
    repairDeviceIds(document, engine_.audioDevices());
    leave();
    moveTo(bundle, std::move(document));
}

void ProjectController::save() {
    settleTakes();
    write();
}

void ProjectController::saveAs(const std::filesystem::path& path) {
    const std::filesystem::path target = namedBundle(path);
    settleTakes();
    if (target == folder_) {
        write();
        return;
    }
    if (std::filesystem::exists(target)) {
        throw AppError(target.string() + " already exists, pass another path");
    }
    write();
    try {
        std::filesystem::create_directories(target.parent_path());
        if (untitled_) {
            moveBundle(folder_, target);
        } else {
            std::filesystem::copy(folder_, target, std::filesystem::copy_options::recursive);
        }
    } catch (const std::filesystem::filesystem_error& error) {
        throw AppError(error.what());
    }
    folder_ = target;
    untitled_ = false;
    scheduleFlush();
    emit fileChanged();
}

std::vector<std::string> ProjectController::removeUnusedAudio() {
    settleTakes();
    try {
        auto removed = app::removeUnusedAudio(project_.document(), folder_);
        // We clear the undo history, because an undo could bring back a clip whose file is gone.
        if (!removed.empty()) {
            project_.reset(project_.document());
        }
        return removed;
    } catch (const std::filesystem::filesystem_error& error) {
        throw AppError(error.what());
    }
}

void ProjectController::scheduleFlush() {
    if (std::exchange(flushPending_, true)) {
        return;
    }
    QMetaObject::invokeMethod(this, &ProjectController::flush, Qt::QueuedConnection);
}

void ProjectController::flush() {
    if (!std::exchange(flushPending_, false)) {
        return;
    }
    publish();
    emit projectChanged();
}

void ProjectController::refreshDevices() {
    const std::vector<engine::AudioDeviceInfo> before = engine_.audioDevices();
    engine_.refreshDevices();
    if (engine_.audioDevices() != before) {
        scheduleFlush();
    }
    emit devicesChanged();
}

void ProjectController::waitForEngine(const std::function<bool()>& reached) {
    if (!processEventsUntil(reached, kTransportTimeout)) {
        qWarning("the engine did not reach the commanded transport state within 500 ms");
    }
    emit transportChanged();
}

void ProjectController::startEngine() {
    try {
        engine_.play();
    } catch (const std::invalid_argument& error) {
        throw AppError(error.what());
    } catch (const std::runtime_error& error) {
        throw AppError(error.what());
    }
}

void ProjectController::play() {
    flush();
    emit aboutToPlay();
    startEngine();
    waitForEngine([this] { return engine_.playing(); });
}

void ProjectController::stop() {
    engine_.stop();
    if (recording_) {
        engine_.setRecording(false);
        recording_ = false;
    }
    // A MIDI take collected before we know the stop position would come out too short, so polling waits.
    timer_.stop();
    waitForEngine([this] { return !engine_.playing(); });
    stopTicks_ = engine_.position().ticks;
    timer_.start(kPollPeriod);
}

void ProjectController::record() {
    if (recording_) {
        return;
    }
    flush();
    for (const model::Track& track : project_.document().tracks) {
        if (track.armed && sessionAudioTracks_.contains(track.id.value)) {
            pendingAudioTakes_.insert(track.id.value);
        }
    }
    engine_.setRecording(true);
    emit aboutToPlay();
    try {
        startEngine();
    } catch (const AppError&) {
        engine_.setRecording(false);
        pendingAudioTakes_.clear();
        throw;
    }
    recording_ = true;
    waitForEngine([this] { return engine_.playing(); });
}

void ProjectController::locate(core::Ticks ticks) {
    flush();
    const core::Ticks target = std::max<core::Ticks>(ticks, 0);
    const double rate = engine_.layout().sampleRate;
    const core::Frames frames = project_.document().tempo.ticksToFrames(target, rate);
    const auto window = static_cast<core::Frames>(rate * kLocateWindowSeconds);
    engine_.locate(target);
    waitForEngine([&] {
        const core::Frames at = engine_.position().frames;
        return at >= frames && at - frames < window;
    });
}

void ProjectController::setLiveMute(const model::TrackId& id, bool value) {
    if (sessionTracks_.contains(id.value)) {
        engine_.setMute(id.value, value);
    }
}

void ProjectController::setLiveSolo(const model::TrackId& id, bool value) {
    if (sessionTracks_.contains(id.value)) {
        engine_.setSolo(id.value, value);
    }
}

void ProjectController::setLiveArmed(const model::TrackId& id, bool value) {
    if (sessionTracks_.contains(id.value)) {
        engine_.setArmed(id.value, value);
    }
}

void ProjectController::setLiveGain(const model::TrackId& id, double db) {
    if (sessionTracks_.contains(id.value)) {
        engine_.setGain(id.value, gainFromDb(db));
    }
}

void ProjectController::setMetronome(const engine::Metronome& metronome) {
    if (metronome != metronome_) {
        metronome_ = metronome;
        scheduleFlush();
    }
}

void ProjectController::publish() {
    engine::Session session = toSession(project_.document(), registry_, folder_);
    session.metronome = metronome_;
    routeErrors_ = dropUnusableDevices(session, engine_.audioDevices(), options_.sampleRate);
    std::set<std::string> ids;
    std::set<std::string> audioIds;
    for (const engine::Track& track : session.tracks) {
        ids.insert(track.id);
        if (track.kind == engine::TrackKind::audio) {
            audioIds.insert(track.id);
        }
    }
    try {
        engine_.setSession(std::move(session));
        sessionTracks_ = std::move(ids);
        sessionAudioTracks_ = std::move(audioIds);
        sessionError_.clear();
    } catch (const std::invalid_argument& error) {
        sessionError_ = error.what();
    } catch (const std::runtime_error& error) {
        sessionError_ = error.what();
    }
    updateTrackErrors();
}

void ProjectController::logStreamChanges() {
    QString summary = "audio closed";
    if (engine_.streamOpen()) {
        const engine::StreamLayout& layout = engine_.layout();
        QStringList devices;
        for (const engine::DeviceChannels& device : layout.devices) {
            const auto info = std::ranges::find(engine_.audioDevices(), device.id, &engine::AudioDeviceInfo::id);
            devices << QString::fromStdString(info != engine_.audioDevices().end() ? info->name : device.id);
        }
        summary = QString("audio open on %1 at %2 Hz, %3 frames, %4 ms out")
                      .arg(devices.join(" + "))
                      .arg(layout.sampleRate)
                      .arg(layout.maxFrames)
                      .arg(std::lround(static_cast<double>(layout.outputLatency) * 1000.0 / layout.sampleRate));
    }
    if (summary != streamSummary_) {
        streamSummary_ = summary;
        qInfo().noquote() << summary;
    }
}

void ProjectController::keepWarmWhileActive() {
    const auto now = std::chrono::steady_clock::now();
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        lastActive_ = now;
    }
    const bool warm = lastActive_.has_value() && now - *lastActive_ < kWarmAfterLeaving;
    if (warm == warm_) {
        return;
    }
    warm_ = warm;
    try {
        engine_.keepWarm(warm);
    } catch (const std::invalid_argument& error) {
        sessionError_ = error.what();
    } catch (const std::runtime_error& error) {
        sessionError_ = error.what();
    }
}

void ProjectController::updateTrackErrors() {
    QVariantMap errors;
    for (const auto* list : {&engine_.sessionErrors(), &std::as_const(routeErrors_)}) {
        for (const engine::SessionError& error : *list) {
            const QString id = QString::fromStdString(error.trackId);
            const QString previous = errors.value(id).toString();
            errors[id] = (previous.isEmpty() ? QString{} : previous + "\n") + QString::fromStdString(error.message);
        }
    }
    if (errors != trackErrors_) {
        trackErrors_ = std::move(errors);
        emit trackErrorsChanged();
    }
}

void ProjectController::poll() {
    if (engine_.devicesChanged()) {
        refreshDevices();
    }
    keepWarmWhileActive();
    engine_.releaseIdleStream();
    logStreamChanges();
    updateTrackErrors();
    collectTakes();
    readLevels();
    const core::Ticks ticks = engine_.position().ticks;
    const bool isPlaying = engine_.playing();
    if (ticks != lastTicks_ || isPlaying != lastPlaying_) {
        lastTicks_ = ticks;
        lastPlaying_ = isPlaying;
        emit transportChanged();
    }
}

void ProjectController::readLevels() {
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = now - lastLevels_;
    lastLevels_ = now;
    meters_.update(engine_.takeInputLevels(), engine_.takeOutputLevels(), elapsed.count());
}

int ProjectController::collectTakes() {
    const std::vector<engine::RecordedTake> takes = engine_.takeRecordings();
    const std::size_t errorsBefore = takeErrors_.size();
    for (const engine::RecordedTake& take : takes) {
        pendingAudioTakes_.erase(take.trackId);
        if (!take.error.empty()) {
            takeErrors_.push_back({take.trackId, take.error});
        }
        if (take.clippedSamples > 0) {
            takeErrors_.push_back({take.trackId, clippedText(take)});
        }
        try {
            addTake(take);
        } catch (const AppError& error) {
            takeErrors_.push_back({take.trackId, std::string{"cannot keep the take: "} + error.what()});
        } catch (const std::filesystem::filesystem_error& error) {
            takeErrors_.push_back({take.trackId, std::string{"cannot keep the take: "} + error.what()});
        }
    }
    if (takeErrors_.size() != errorsBefore) {
        emit projectChanged();
    }
    return static_cast<int>(takes.size());
}

void ProjectController::addTake(const engine::RecordedTake& take) {
    const model::TrackId id{take.trackId};
    const model::Track* track = project_.track(id);
    const TrackType* type = track == nullptr ? nullptr : registry_.trackType(track->kind);
    if (type == nullptr || !type->fromTake) {
        return;
    }
    const TakeContext context{.tempo = project_.document().tempo,
                              .folder = folder_,
                              .stopTicks = stopTicks_,
                              .sampleRate = engine_.layout().sampleRate};
    project_.editTrack(id, "record take on " + track->name,
                       [&](model::Track& edited) { type->fromTake(edited, take, context); });
}

QString ProjectController::positionText() const {
    const core::BarBeatTick position = project_.document().tempo.toBarBeatTick(engine_.position().ticks);
    return QStringLiteral("%1 . %2 . %3").arg(position.bar).arg(position.beat).arg(position.tick, 3, 10, QChar('0'));
}

QString ProjectController::tempoText() const {
    const core::TempoMap& tempo = project_.document().tempo;
    return QStringLiteral("%1   %2/%3").arg(tempo.bpm).arg(tempo.meter.numerator).arg(tempo.meter.denominator);
}

qint64 ProjectController::endTicks() const { return projectEnd(project_.document()); }

qint64 ProjectController::snapped(qint64 ticks) const {
    const core::Ticks beat = ticksPerBeat();
    const core::Ticks result =
        registry_.settingValue("edit.snap").get<bool>() ? (ticks + beat / 2) / beat * beat : ticks;
    return std::max<core::Ticks>(result, 0);
}

QString ProjectController::ticksText(qint64 ticks, bool duration) const {
    const core::TempoMap& tempo = project_.document().tempo;
    return qs(duration ? durationText(ticks, tempo) : tempo.toBarBeatTick(ticks).format());
}

qint64 ProjectController::parseTicks(const QString& text, bool duration) const {
    bool number = false;
    const qint64 ticks = text.trimmed().toLongLong(&number);
    if (number) {
        return ticks;
    }
    try {
        core::BarBeatTick position = core::BarBeatTick::parse(text.trimmed().toStdString());
        if (duration) {
            position = {position.bar + 1, position.beat + 1, position.tick};
        }
        return project_.document().tempo.fromBarBeatTick(position);
    } catch (const std::invalid_argument&) {
        return -1;
    }
}

QVariantMap ProjectController::routeDevices() const {
    return toVariant(app::routeDevices(devicesState(*this))).toMap();
}

}  // namespace emerick::app
