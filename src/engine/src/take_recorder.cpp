#include "take_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace emerick::engine {

namespace {

constexpr double kRecordRingSeconds = 2.0;
constexpr std::size_t kMidiRingBytes = std::size_t{1} << 18;
constexpr Frames kSilenceChunkFrames = Frames{1} << 16;

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
                           Frames captureOffset, std::filesystem::path folder, TakeInbox& inbox)
    : trackId_(std::move(trackId)),
      trackKind_(kind),
      channels_(channels),
      sampleRate_(sampleRate),
      captureOffset_(captureOffset),
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

void TakeRecorder::begin(Ticks startTick, Frames delay) {
    owedFrames_ = 0;
    lostMidi_ = 0;
    const Frames skip = captureOffset_ - delay;
    framesToSkip_ = std::max<Frames>(skip, 0);
    recording_ = writeKeepingRoomForEnd(Header{Kind::begin, 0, startTick, 0, std::max<Frames>(-skip, 0)},
                                        std::span<const std::uint8_t>{});
}

void TakeRecorder::writeAudio(std::span<const float* const> channels, Frames frames) {
    const Frames skipped = std::min(framesToSkip_, frames);
    framesToSkip_ -= skipped;
    frames -= skipped;
    if (frames == 0) {
        return;
    }
    const auto width = channels.size();
    for (std::size_t channel = 0; channel < width; ++channel) {
        for (Frames frame = 0; frame < frames; ++frame) {
            interleaved_[static_cast<std::size_t>(frame) * width + channel] = channels[channel][skipped + frame];
        }
    }
    const auto samples = std::span<const float>(interleaved_).first(static_cast<std::size_t>(frames) * width);
    if (writeKeepingRoomForEnd(Header{Kind::audio, static_cast<std::uint32_t>(frames), 0, owedFrames_}, samples)) {
        owedFrames_ = 0;
    } else {
        owedFrames_ += frames;
    }
}

void TakeRecorder::writeMidi(Ticks tick, std::span<const std::uint8_t> bytes) {
    if (!writeKeepingRoomForEnd(Header{Kind::midi, static_cast<std::uint32_t>(bytes.size()), tick, 0}, bytes)) {
        ++lostMidi_;
    }
}

template <class T>
bool TakeRecorder::writeKeepingRoomForEnd(const Header& header, std::span<const T> payload) {
    return ring_.writable() >= 2 * sizeof(Header) + payload.size_bytes() && writeRecord(ring_, header, payload);
}

void TakeRecorder::end() {
    writeRecord(ring_, Header{Kind::end, lostMidi_, 0, owedFrames_});
    recording_ = false;
}

void TakeRecorder::poll() {
    Header header;
    while (readValue(ring_, header)) {
        switch (header.kind) {
            case Kind::begin:
                finish();
                start(header.tick);
                writeZeros(header.leadFrames);
                break;
            case Kind::audio:
                writeSilence(header.owedFrames);
                writeSamples(header.size);
                break;
            case Kind::midi:
                bytes_.resize(header.size);
                readInto(ring_, std::span(bytes_));
                if (take_) {
                    take_->midiEvents.push_back({header.tick - take_->startTick, bytes_});
                    const std::scoped_lock lock(progressMutex_);
                    if (progress_) {
                        progress_->midiEvents.push_back(take_->midiEvents.back());
                    }
                }
                break;
            case Kind::end:
                writeSilence(header.owedFrames);
                if (take_) {
                    take_->droppedMidiEvents = header.size;
                }
                finish();
                break;
        }
    }
}

void TakeRecorder::start(Ticks startTick) {
    take_ = RecordedTake{.trackId = trackId_,
                         .startTick = startTick,
                         .audioFile = {},
                         .midiEvents = {},
                         .error = {},
                         .samples = 0,
                         .clippedSamples = 0,
                         .nonFiniteSamples = 0,
                         .droppedFrames = 0,
                         .droppedMidiEvents = 0};
    peakFrames_ = 0;
    std::filesystem::path audioFile;
    if (trackKind_ == TrackKind::audio) {
        const std::filesystem::path path = freeTakePath(folder_, trackId_);
        SF_INFO info{};
        info.samplerate = static_cast<int>(sampleRate_);
        info.channels = channels_;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        file_.reset(openSoundFile(path, SFM_WRITE, &info));
        if (file_) {
            audioFile = path;
            take_->audioFile = path;
        } else {
            markDamaged("cannot write " + path.string() + ": " + sf_strerror(nullptr));
        }
    }
    const std::scoped_lock lock(progressMutex_);
    progress_ = TakeProgress{.trackId = trackId_,
                             .take = ++takes_,
                             .startTick = startTick,
                             .audioFile = std::move(audioFile),
                             .frames = 0,
                             .midiEvents = {},
                             .peakMinimum = {},
                             .peakMaximum = {}};
}

std::optional<TakeProgress> TakeRecorder::takeProgress() {
    const std::scoped_lock lock(progressMutex_);
    if (!progress_) {
        return std::nullopt;
    }
    TakeProgress read = *progress_;
    progress_->midiEvents.clear();
    progress_->peakMinimum.clear();
    progress_->peakMaximum.clear();
    return read;
}

void TakeRecorder::writeSilence(Frames frames) {
    if (take_) {
        take_->droppedFrames += frames;
    }
    writeZeros(frames);
}

void TakeRecorder::writeZeros(Frames frames) {
    if (!take_ || frames == 0) {
        return;
    }
    while (frames > 0) {
        const auto chunk = static_cast<std::uint32_t>(std::min(frames, kSilenceChunkFrames));
        samples_.assign(static_cast<std::size_t>(chunk) * static_cast<std::size_t>(channels_), 0.0F);
        writeToFile(chunk);
        frames -= chunk;
    }
}

void TakeRecorder::writeSamples(std::uint32_t frames) {
    samples_.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_));
    readInto(ring_, std::span(samples_));
    if (!take_) {
        return;
    }
    const auto nonFinite = [](float sample) { return !std::isfinite(sample); };
    take_->nonFiniteSamples += static_cast<std::uint64_t>(std::ranges::count_if(samples_, nonFinite));
    std::ranges::replace_if(samples_, nonFinite, 0.0F);
    take_->samples += samples_.size();
    take_->clippedSamples += static_cast<std::uint64_t>(
        std::ranges::count_if(samples_, [](float sample) { return std::abs(sample) >= kFullScale; }));
    writeToFile(frames);
}

void TakeRecorder::writeToFile(std::uint32_t frames) {
    if (!take_ || !file_) {
        return;
    }
    if (sf_writef_float(file_.get(), samples_.data(), frames) != frames) {
        markDamaged("cannot write all audio to " + take_->audioFile.string());
    }
    addPeaks(frames);
}

void TakeRecorder::addPeaks(std::uint32_t frames) {
    const auto width = static_cast<std::size_t>(channels_);
    std::vector<float> minimum;
    std::vector<float> maximum;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto [low, high] = std::ranges::minmax(std::span(samples_).subspan(frame * width, width));
        peakLow_ = peakFrames_ == 0 ? low : std::min(peakLow_, low);
        peakHigh_ = peakFrames_ == 0 ? high : std::max(peakHigh_, high);
        if (++peakFrames_ == kTakePeakFrames) {
            minimum.push_back(peakLow_);
            maximum.push_back(peakHigh_);
            peakFrames_ = 0;
        }
    }
    const std::scoped_lock lock(progressMutex_);
    if (!progress_) {
        return;
    }
    progress_->frames += frames;
    progress_->peakMinimum.insert(progress_->peakMinimum.end(), minimum.begin(), minimum.end());
    progress_->peakMaximum.insert(progress_->peakMaximum.end(), maximum.begin(), maximum.end());
}

void TakeRecorder::markDamaged(std::string error) {
    if (take_ && take_->error.empty()) {
        take_->error = std::move(error);
    }
}

// We end the progress before the take reaches the inbox, so a reader that sees the take never sees its progress again.
void TakeRecorder::finish() {
    {
        const std::scoped_lock lock(progressMutex_);
        progress_.reset();
    }
    file_.reset();
    if (take_ && (trackKind_ == TrackKind::audio || !take_->midiEvents.empty())) {
        inbox_.add(std::move(*take_));
    }
    take_.reset();
}

}  // namespace emerick::engine
