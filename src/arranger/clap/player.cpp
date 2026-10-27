#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "emerick/core/time.hpp"
#include "emerick/yamaha/section.hpp"

namespace emerick::arranger {

namespace {

using core::Ticks;
using yamaha::Section;

// A block that starts more than this far from where the last one ended follows a locate or a cycle jump.
constexpr double kJumpTolerance = 1.0;
constexpr std::size_t kCapacity = 4096;
// The host's beat positions carry rounding, so a loop end a hair past a frame still lands on it.
constexpr double kFrameEpsilon = 1e-6;

Ticks floorTo(Ticks value, Ticks unit) { return (value >= 0 ? value / unit : -((-value + unit - 1) / unit)) * unit; }

Ticks ceilTo(Ticks value, Ticks unit) { return -floorTo(-value, unit); }

}  // namespace

Player::Player() { out_.reserve(kCapacity); }

void Player::begin(const Block& block, Arranger* arranger, bool autoFill) {
    out_.clear();
    lastFrame_ = 0;
    block_ = block;
    baseTicks_ = block.ticks;
    baseFrame_ = 0;
    jumpFrame_.reset();
    restartFrame_.reset();
    bool restart = !wasPlaying_ || std::abs(block.ticks - expected_) > kJumpTolerance;
    if (arranger != arranger_) {
        if (arranger_ != nullptr) {
            stopAt(cursor_, 0);
        }
        arranger_ = arranger;
        if (arranger_ != nullptr) {
            arranger_->sendVoices(0);
            emit(arranger_->render(0, 0), 0);
        }
        restart = true;
    }
    wasPlaying_ = block.playing;
    if (arranger_ == nullptr) {
        return;
    }
    arranger_->setAutoFill(autoFill);
    if (!block.playing || restart) {
        stopAt(cursor_, 0);
        deferredStart_.reset();
    }
    if (!block.playing) {
        return;
    }
    if (restart) {
        restartFrame_ = 0;
    }
    if (block.looping && block.ticks < block.loopEnd && block.ticksPerFrame > 0.0) {
        const double end = block.ticks + (static_cast<double>(block.frames) * block.ticksPerFrame);
        if (end > block.loopEnd) {
            jumpFrame_ = static_cast<std::uint32_t>(
                std::ceil(((block.loopEnd - block.ticks) / block.ticksPerFrame) - kFrameEpsilon));
        }
    }
}

void Player::chord(std::uint32_t frame, const yamaha::Chord& chord) {
    reach(frame);
    targetChord_ = chord;
    if (live()) {
        arranger_->setChord(chord, tickAt(frame));
    }
}

void Player::section(std::uint32_t frame, const yamaha::SectionPress& press) {
    reach(frame);
    remember(press);
    if (!live()) {
        return;
    }
    const Ticks at = tickAt(frame);
    if (arranger_->playing()) {
        arranger_->request(press.kind, press.variation, at);
    } else if (yamaha::startsSection(press.kind)) {
        deferredStart_ = ceilTo(at, block_.ticksPerBar);
    }
}

std::span<const Player::Output> Player::finish() {
    reach(block_.frames);
    expected_ = baseTicks_ + (static_cast<double>(block_.frames - baseFrame_) * block_.ticksPerFrame);
    return out_;
}

Ticks Player::tickAt(std::uint32_t frame) const {
    return std::llround(baseTicks_ +
                        ((static_cast<double>(frame) - static_cast<double>(baseFrame_)) * block_.ticksPerFrame));
}

bool Player::live() const { return arranger_ != nullptr && block_.playing && !restartFrame_; }

void Player::remember(const yamaha::SectionPress& press) {
    switch (press.kind) {
        case Section::Main:
        case Section::FillTo:
            target_ = Request{Section::Main, press.variation};
            playingMain_ = press.variation;
            return;
        case Section::Intro:
            target_ = Request{press.kind, press.variation};
            return;
        case Section::Ending:
            target_ = Request{press.kind, press.variation};
            playingMain_.reset();
            return;
        case Section::Fill:
        case Section::Break:
            return;
    }
}

// Before an event at frame: jumps back at the loop end, starts over where a jump left the transport, and plays up to
// frame. Events at the frame of a jump only tell where to start over.
void Player::reach(std::uint32_t frame) {
    if (arranger_ == nullptr || !block_.playing) {
        return;
    }
    if (jumpFrame_ && frame >= *jumpFrame_ && *jumpFrame_ < block_.frames) {
        const std::uint32_t jump = *jumpFrame_;
        jumpFrame_.reset();
        renderTo(jump);
        stopAt(tickAt(jump), jump);
        deferredStart_.reset();
        baseTicks_ = block_.loopStart;
        baseFrame_ = jump;
        restartFrame_ = jump;
    }
    if (restartFrame_) {
        if (frame <= *restartFrame_) {
            return;
        }
        const Ticks at = tickAt(*restartFrame_);
        restartFrame_.reset();
        cursor_ = at;
        if (target_) {
            startAt(at);
        }
    }
    renderTo(frame);
}

void Player::renderTo(std::uint32_t frame) {
    const Ticks to = tickAt(frame);
    if (!arranger_->playing()) {
        if (!deferredStart_ || *deferredStart_ >= to) {
            return;
        }
        cursor_ = *deferredStart_;
        deferredStart_.reset();
        startAt(cursor_);
    }
    if (to > cursor_) {
        emit(arranger_->render(cursor_, to), std::nullopt);
        cursor_ = to;
    }
}

// We start at the bar line before at, so the parts play from where the bar would have them.
void Player::startAt(Ticks at) {
    arranger_->forget();
    if (target_) {
        arranger_->request(target_->kind, target_->variation, at);
    }
    if (targetChord_) {
        arranger_->setChord(*targetChord_, at);
    }
    arranger_->start(floorTo(at, block_.ticksPerBar));
}

void Player::stopAt(Ticks at, std::uint32_t frame) {
    arranger_->stop(at);
    emit(arranger_->render(at, at), frame);
}

void Player::emit(std::span<const MidiEvent> events, std::optional<std::uint32_t> frame) {
    const std::uint32_t last = block_.frames == 0 ? 0 : block_.frames - 1;
    for (const MidiEvent& event : events) {
        std::int64_t at = frame.value_or(baseFrame_);
        if (!frame && block_.ticksPerFrame > 0.0) {
            at += static_cast<std::int64_t>(
                std::floor((static_cast<double>(event.at) - baseTicks_) / block_.ticksPerFrame));
        }
        const auto placed = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(at, std::max(lastFrame_, baseFrame_), static_cast<std::int64_t>(last)));
        if (out_.size() < out_.capacity()) {
            out_.push_back({.frame = placed, .bytes = event.bytes});
        }
        lastFrame_ = placed;
    }
}

}  // namespace emerick::arranger
