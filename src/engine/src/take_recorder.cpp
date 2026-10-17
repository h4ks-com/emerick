#include "take_recorder.hpp"

#include <cmath>
#include <utility>

namespace emerick::engine {

namespace {

constexpr double kRecordRingSeconds = 2.0;
constexpr std::size_t kMidiRingBytes = std::size_t{1} << 18;

std::filesystem::path freeTakePath(const std::filesystem::path& folder, const std::string& trackId) {
    for (int number = 1;; ++number) {
        std::filesystem::path path = folder / (trackId + "-take-" + std::to_string(number) + ".wav");
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return path;
        }
    }
}

}  // namespace

void TakeInbox::add(RecordedTake take) {
    const std::scoped_lock lock(mutex_);
    takes_.push_back(std::move(take));
}

std::vector<RecordedTake> TakeInbox::take() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(takes_, {});
}

TakeRecorder::TakeRecorder(std::string trackId, TrackKind kind, int channels, double sampleRate, Frames maxBlock,
                           std::filesystem::path folder, TakeInbox& inbox)
    : trackId_(std::move(trackId)),
      trackKind_(kind),
      channels_(channels),
      sampleRate_(sampleRate),
      folder_(std::move(folder)),
      inbox_(inbox),
      ring_(kind == TrackKind::audio ? static_cast<std::size_t>(std::llround(kRecordRingSeconds * sampleRate)) *
                                           static_cast<std::size_t>(channels) * sizeof(float)
                                     : kMidiRingBytes),
      interleaved_(static_cast<std::size_t>(maxBlock * channels)) {}

// We destroy a recorder only after the audio thread releases it, so we can finish its take here. Only allocation and
// locking throw in here, and we treat their failure as fatal.
// NOLINTNEXTLINE(bugprone-exception-escape)
TakeRecorder::~TakeRecorder() {
    poll();
    finish();
}

void TakeRecorder::begin(Ticks startTick) {
    dropped_ = 0;
    recording_ = writeKeepingRoomForEnd(Header{Kind::begin, 0, startTick}, std::span<const std::uint8_t>{});
}

void TakeRecorder::writeAudio(std::span<const float* const> channels, Frames frames) {
    const auto width = channels.size();
    for (std::size_t channel = 0; channel < width; ++channel) {
        for (Frames frame = 0; frame < frames; ++frame) {
            interleaved_[static_cast<std::size_t>(frame) * width + channel] = channels[channel][frame];
        }
    }
    const auto samples = std::span<const float>(interleaved_).first(static_cast<std::size_t>(frames) * width);
    writeKeepingRoomForEnd(Header{Kind::audio, static_cast<std::uint32_t>(frames), 0}, samples);
}

void TakeRecorder::writeMidi(Ticks tick, std::span<const std::uint8_t> bytes) {
    writeKeepingRoomForEnd(Header{Kind::midi, static_cast<std::uint32_t>(bytes.size()), tick}, bytes);
}

template <class T>
bool TakeRecorder::writeKeepingRoomForEnd(const Header& header, std::span<const T> payload) {
    if (ring_.writable() >= 2 * sizeof(Header) + payload.size_bytes() && writeRecord(ring_, header, payload)) {
        return true;
    }
    ++dropped_;
    return false;
}

void TakeRecorder::end() {
    writeRecord(ring_, Header{Kind::end, dropped_, 0});
    recording_ = false;
}

void TakeRecorder::poll() {
    Header header;
    while (readValue(ring_, header)) {
        switch (header.kind) {
            case Kind::begin:
                finish();
                start(header.tick);
                break;
            case Kind::audio:
                writeSamples(header.size);
                break;
            case Kind::midi:
                bytes_.resize(header.size);
                readInto(ring_, std::span(bytes_));
                if (take_) {
                    take_->midiEvents.push_back({header.tick - take_->startTick, bytes_});
                }
                break;
            case Kind::end:
                if (header.size > 0) {
                    markDamaged("the recording buffer overflowed and " + std::to_string(header.size) +
                                " blocks were lost");
                }
                finish();
                break;
        }
    }
}

void TakeRecorder::start(Ticks startTick) {
    take_ = RecordedTake{.trackId = trackId_, .startTick = startTick, .audioFile = {}, .midiEvents = {}, .error = {}};
    if (trackKind_ != TrackKind::audio) {
        return;
    }
    const std::filesystem::path path = freeTakePath(folder_, trackId_);
    SF_INFO info{};
    info.samplerate = static_cast<int>(sampleRate_);
    info.channels = channels_;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    file_.reset(sf_open(path.c_str(), SFM_WRITE, &info));
    if (file_) {
        take_->audioFile = path;
    } else {
        markDamaged("cannot write " + path.string() + ": " + sf_strerror(nullptr));
    }
}

void TakeRecorder::writeSamples(std::uint32_t frames) {
    samples_.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_));
    readInto(ring_, std::span(samples_));
    if (take_ && file_ && sf_writef_float(file_.get(), samples_.data(), frames) != frames) {
        markDamaged("cannot write all audio to " + take_->audioFile.string());
    }
}

void TakeRecorder::markDamaged(std::string error) {
    if (take_ && take_->error.empty()) {
        take_->error = std::move(error);
    }
}

void TakeRecorder::finish() {
    file_.reset();
    if (take_ && (trackKind_ == TrackKind::audio || !take_->midiEvents.empty())) {
        inbox_.add(std::move(*take_));
    }
    take_.reset();
}

}  // namespace emerick::engine
