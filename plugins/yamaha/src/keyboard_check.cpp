#include "keyboard_check.hpp"

#include <QMetaObject>
#include <QStringList>
#include <QtLogging>
#include <algorithm>
#include <ranges>
#include <span>
#include <stdexcept>

#include "emerick/core/midi.hpp"
#include "emerick/yamaha/profile.hpp"
#include "emerick/yamaha/sysex.hpp"
#include "registration.hpp"

namespace emerick::yamaha {

namespace {

// We judge after about four bars of the Style's Bass, Chord, Pad and Phrase notes.
constexpr int kStyleNotesToJudge = 48;
constexpr core::Frames kMonitoringFrames = 256;
constexpr std::string_view kInstallTemplate = "Install the keyboard template from Plugins > Yamaha Arranger";

int channelOf(std::string_view name) { return std::ranges::find(genos2Parts(), name, &Part::name)->channel; }

bool isNoteOn(std::span<const std::uint8_t> bytes) {
    return bytes.size() == 3 && (bytes[0] & 0xF0) == core::midi::kNoteOn && bytes[2] > 0;
}

}  // namespace

KeyboardCheck::KeyboardCheck(app::ProjectController& controller) : controller_(controller) {
    QObject::connect(&controller_, &app::ProjectController::devicesChanged, this, &KeyboardCheck::hintsChanged);
    QObject::connect(&controller_, &app::ProjectController::projectChanged, this, &KeyboardCheck::hintsChanged);
    QObject::connect(&controller_, &app::ProjectController::aboutToPlay, this, [this] {
        const auto& tracks = controller_.project().document().tracks;
        const auto chords = std::ranges::find(tracks, std::string{kChordKind}, &model::Track::kind);
        if (chords != tracks.end() && !chords->midiIn.portId.empty()) {
            listen(chords->midiIn.portId);
        }
    });
}

void KeyboardCheck::listen(const std::string& port) {
    input_.reset();
    channels_.clear();
    heardChords_ = false;
    styleNotes_ = 0;
    try {
        input_ = controller_.engine().midi().openInput(port, [this](std::span<const std::uint8_t> bytes, std::int64_t) {
            QMetaObject::invokeMethod(
                this, [this, message = std::vector(bytes.begin(), bytes.end())] { receive(message); },
                Qt::QueuedConnection);
        });
    } catch (const std::invalid_argument& error) {
        qWarning("%s", error.what());
    } catch (const std::runtime_error& error) {
        qWarning("%s", error.what());
    }
}

void KeyboardCheck::receive(const std::vector<std::uint8_t>& bytes) {
    if (!input_) {
        return;
    }
    heardChords_ = heardChords_ || decodeChordControl(bytes).has_value();
    if (!isNoteOn(bytes)) {
        return;
    }
    const int channel = (bytes[0] & 0x0F) + 1;
    channels_.insert(channel);
    if (channel >= channelOf("Bass")) {
        ++styleNotes_;
    }
    if (styleNotes_ >= kStyleNotesToJudge) {
        judge();
    }
}

void KeyboardCheck::judge() {
    input_.reset();
    keyboardHints_.clear();
    if (!channels_.contains(channelOf("Rhythm 1")) && !channels_.contains(channelOf("Rhythm 2"))) {
        keyboardHints_.push_back(std::string{kInstallTemplate} + " so the keyboard sends its Rhythm parts");
    }
    if (channels_.contains(channelOf("Left")) && !heardChords_) {
        keyboardHints_.push_back(std::string{kInstallTemplate} + " so the keyboard sends its chords");
    }
    emit hintsChanged();
}

std::vector<std::string> KeyboardCheck::hints() const {
    std::vector<std::string> result;
    const model::Document& document = controller_.project().document();
    if (const auto buttons = styleButtons(document);
        buttons && std::ranges::any_of(document.tracks, [&](const auto& track) {
            return track.kind == "midi" && track.midiOut.portId == buttons->portId &&
                   track.midiOut.channel == buttons->channel;
        })) {
        result.push_back(std::string{kInstallTemplate} +
                         ", then route the parts to the keyboard's Port 2 and the Style track to its Port 1, so part "
                         "playback presses no Style buttons");
    }
    result.insert(result.end(), keyboardHints_.begin(), keyboardHints_.end());
    if (controller_.engine().layout().maxFrames > kMonitoringFrames) {
        result.emplace_back(
            "Set Audio buffer size to 256 or less in Settings > Audio and restart Emerick to monitor "
            "the keyboard with less delay");
    }
    return result;
}

QString KeyboardCheck::message() const {
    QStringList lines;
    for (const std::string& hint : hints()) {
        lines.append(app::qs(hint));
    }
    return lines.join('\n');
}

}  // namespace emerick::yamaha
