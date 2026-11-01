#include "style_sync.hpp"

#include <QMetaObject>
#include <QtLogging>
#include <algorithm>
#include <span>
#include <stdexcept>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/profile.hpp"
#include "emerick/yamaha/render.hpp"
#include "emerick/yamaha/sysex.hpp"
#include "midi_listener.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

constexpr const char* kSyncSource = "yamaha";
// ponytail: a fixed window; a Style with only long held notes and no drums can read as stopped.
constexpr auto kRunningWindow = std::chrono::seconds{4};
constexpr auto kStartGrace = std::chrono::seconds{1};
constexpr auto kStopGrace = std::chrono::seconds{2};

bool isStyleNote(std::span<const std::uint8_t> bytes) {
    return bytes.size() == 3 && (bytes[0] & 0xF0) == core::midi::kNoteOn && bytes[2] > 0 &&
           (bytes[0] & 0x0F) + 1 >= partChannel("Rhythm 1");
}

std::string keyboardPort(const model::Document& document) {
    const auto track = std::ranges::find_if(document.tracks, [](const model::Track& candidate) {
        return (candidate.kind == kChordKind || candidate.kind == kStyleKind) && !candidate.midiIn.portId.empty();
    });
    return track == document.tracks.end() ? std::string{} : track->midiIn.portId;
}

std::string patternText(const PatternPosition& pattern) {
    return sectionName(pattern.section) + " " + std::to_string(pattern.bar) +
           (pattern.bars > 0 ? "/" + std::to_string(pattern.bars) : std::string{});
}

}  // namespace

StyleSync::StyleSync(app::ProjectController& controller, const app::Registry& registry)
    : controller_(controller), registry_(registry) {
    QObject::connect(&controller_, &app::ProjectController::projectChanged, this, &StyleSync::follow);
    QObject::connect(&controller_, &app::ProjectController::projectChanged, this, &StyleSync::silenceMutedStyle);
    QObject::connect(&controller_, &app::ProjectController::devicesChanged, this, &StyleSync::follow);
    QObject::connect(&controller_, &app::ProjectController::transportChanged, this, &StyleSync::report);
    QObject::connect(&controller_, &app::ProjectController::aboutToPlay, this, [this] {
        playStarted_ = Clock::now();
        lastStop_ = {};
        keyboardMain_.reset();
    });
    follow();
}

bool StyleSync::styleRunning() const { return Clock::now() - lastStyleNote_ < kRunningWindow; }

void StyleSync::follow() {
    const std::string port = keyboardPort(controller_.project().document());
    if (port == port_ && input_) {
        return;
    }
    port_ = port;
    input_.reset();
    if (!port_.empty()) {
        input_ = listenOnPort(controller_, port_, *this,
                              [this](const std::vector<std::uint8_t>& message) { receive(message); });
    }
}

void StyleSync::receive(const std::vector<std::uint8_t>& bytes) {
    if (isStyleNote(bytes)) {
        lastStyleNote_ = Clock::now();
        silenceMutedStyle();
    } else if (const auto press = decodeSectionControl(bytes); press && press->on && press->kind == Section::Main) {
        keyboardMain_ = press->variation;
    }
}

// MIDI Start makes the keyboard play its Style, so a muted Style track stops it with the Start/Stop button.
void StyleSync::silenceMutedStyle() {
    if (!controller_.playing() || !styleRunning() || Clock::now() - lastStop_ < kStopGrace) {
        return;
    }
    const model::Track* style = mutedStyle(controller_.project().document());
    const app::TrackType* type = registry_.trackType(kStyleKind);
    if (style == nullptr || type == nullptr || !type->panic) {
        return;
    }
    for (const app::MidiMessage& message : type->panic(*style)) {
        try {
            controller_.engine().sendMidi(message.port.empty() ? style->midiOut.portId : message.port, message.bytes);
        } catch (const std::invalid_argument& error) {
            qWarning("cannot stop the Style: %s", error.what());
        } catch (const std::runtime_error& error) {
            qWarning("cannot stop the Style: %s", error.what());
        }
    }
    lastStop_ = Clock::now();
    lastStyleNote_ = {};
}

void StyleSync::report() {
    const model::Document& document = controller_.project().document();
    const auto style = playingStyle(document);
    const core::Ticks position = controller_.positionTicks();
    const auto pattern =
        style && controller_.playing() ? patternAt(*style, position, document.tempo.ticksPerBar()) : std::nullopt;
    if (!pattern) {
        show({});
        return;
    }
    const auto chord = chordAt(document, position);
    app::SyncStatus status{.label = patternText(*pattern) + (chord ? " " + chord->name() : std::string{}),
                           .detail = sectionName(pattern->section) + ", bar " + std::to_string(pattern->bar) +
                                     (pattern->bars > 0 ? " of " + std::to_string(pattern->bars) : std::string{}) +
                                     (chord ? "\nChord " + chord->name() : std::string{}),
                           .ok = true};
    // A fill or a break leads to a Main the keyboard reports only at the next bar line, and an Ending stops the Style.
    const auto expected = chaseSection(*style, position);
    const bool keyboardPlaysOtherMain = keyboardMain_ && expected && expected->kind == Section::Main &&
                                        expected->at == pattern->section.at && *keyboardMain_ != expected->variation;
    if (pattern->section.kind != Section::Ending &&
        Clock::now() - playStarted_ >
            kStartGrace + std::chrono::duration<double>(document.tempo.ticksToSeconds(document.tempo.ticksPerBar())) &&
        !styleRunning()) {
        status.ok = false;
        status.detail +=
            "\nInstall the keyboard template from Plugins > Yamaha Arranger so the keyboard follows Emerick's clock "
            "and Start: it plays no Style";
    } else if (keyboardPlaysOtherMain && position - pattern->section.at > document.tempo.ticksPerBar()) {
        status.ok = false;
        status.detail +=
            "\nInstall the keyboard template from Plugins > Yamaha Arranger so the keyboard follows Emerick's "
            "sections: it plays " +
            sectionName({0, Section::Main, *keyboardMain_});
    }
    show(std::move(status));
}

void StyleSync::show(app::SyncStatus status) {
    if (status.label == shown_.label && status.detail == shown_.detail && status.ok == shown_.ok) {
        return;
    }
    shown_ = status;
    controller_.setSyncStatus(kSyncSource, std::move(status));
}

}  // namespace emerick::yamaha
