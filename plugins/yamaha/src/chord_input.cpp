#include "emerick/yamaha/chord_input.hpp"

#include <QMetaObject>
#include <QtLogging>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/profile.hpp"
#include "emerick/yamaha/sysex.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

constexpr std::size_t kNoteLength = 3;

}  // namespace

ChordInput::ChordInput(app::Registry& registry, app::ProjectController& controller, app::Nav& nav)
    : registry_(registry), controller_(controller), nav_(nav) {
    QObject::connect(&nav_, &app::Nav::changed, &context_, [this] { follow(); });
}

void ChordInput::follow() {
    const QVariantMap block = nav_.editingBlock();
    if (block == block_) {
        return;
    }
    block_ = block;
    input_.reset();
    held_.clear();
    played_.clear();
    const model::Track* track =
        controller_.project().track(model::TrackId{block.value("track").toString().toStdString()});
    if (track != nullptr && track->kind == kChordKind && !track->midiIn.portId.empty()) {
        listen(*track);
    }
}

namespace {

// The keyboard plays its chords with the left hand, so a Chords track that listens on every channel reads that part.
int leftHandChannel() { return std::ranges::find(genos2Parts(), "Left", &Part::name)->channel; }

}  // namespace

void ChordInput::listen(const model::Track& track) {
    channel_ = track.midiIn.channel != 0 ? track.midiIn.channel : leftHandChannel();
    try {
        input_ = controller_.engine().midi().openInput(
            track.midiIn.portId, [this](std::span<const std::uint8_t> bytes, std::int64_t) {
                QMetaObject::invokeMethod(
                    &context_, [this, message = std::vector(bytes.begin(), bytes.end())] { receive(message); },
                    Qt::QueuedConnection);
            });
    } catch (const std::invalid_argument& error) {
        qWarning("%s", error.what());
    }
}

void ChordInput::receive(const std::vector<std::uint8_t>& bytes) {
    if (const auto chord = decodeChordControl(bytes)) {
        set(*chord);
        return;
    }
    if (bytes.size() != kNoteLength || (bytes[0] & 0x0F) + 1 != channel_) {
        return;
    }
    const auto type = static_cast<std::uint8_t>(bytes[0] & 0xF0);
    const int note = bytes[1];
    if (type == core::midi::kNoteOn && bytes[2] > 0) {
        held_.insert(note);
        played_.push_back(note);
    } else if (type == core::midi::kNoteOff || type == core::midi::kNoteOn) {
        held_.erase(note);
        if (held_.empty() && !played_.empty()) {
            const auto chord = recognizeChord(played_);
            played_.clear();
            if (chord) {
                set(*chord);
            }
        }
    }
}

void ChordInput::set(const Chord& chord) {
    try {
        registry_.run("yamaha.chord.set", {{"track", block_.value("track").toString().toStdString()},
                                           {"at", block_.value("at").toLongLong()},
                                           {"chord", chord.name()}});
    } catch (const app::AppError& error) {
        qWarning("%s", error.what());
    }
    nav_.setEditingBlock({});
}

}  // namespace emerick::yamaha
