#include "clip_stream.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace emerick::engine {

namespace {

constexpr Frames kReadChunkFrames = 4096;
constexpr double kReadAheadSeconds = 2.0;

}  // namespace

std::expected<ClipFile, std::string> openClipFile(const std::filesystem::path& path, double sampleRate) {
    ClipFile opened;
    opened.file.reset(sf_open(path.c_str(), SFM_READ, &opened.info));
    if (!opened.file) {
        return std::unexpected("cannot open audio clip " + path.string());
    }
    if (opened.info.samplerate != static_cast<int>(sampleRate)) {
        return std::unexpected("audio clip " + path.string() + " is at " + std::to_string(opened.info.samplerate) +
                               " Hz and the stream runs at " + std::to_string(static_cast<int>(sampleRate)) + " Hz");
    }
    return opened;
}

ClipStream::ClipStream(ClipFile opened, Frames sourceOffset, Frames length, double sampleRate, Frames maxBlock)
    : file_(std::move(opened.file)),
      channels_(opened.info.channels),
      sourceOffset_(sourceOffset),
      length_(std::clamp<Frames>(opened.info.frames - sourceOffset, 0, length)),
      ring_(bytesFor(std::llround(kReadAheadSeconds * sampleRate))),
      scratch_(samplesFor(maxBlock)),
      readBuffer_(samplesFor(kReadChunkFrames)) {
    sf_seek(file_.get(), sourceOffset_, SEEK_SET);
}

std::size_t ClipStream::samplesFor(Frames frames) const { return static_cast<std::size_t>(frames * channels_); }

std::size_t ClipStream::bytesFor(Frames frames) const { return samplesFor(frames) * sizeof(float); }

bool ClipStream::buffered() const {
    return bufferedGeneration_.load(std::memory_order_acquire) == generation_.load(std::memory_order_acquire);
}

void ClipStream::seek(Frames local) {
    ring_.skip(ring_.readable());
    remaining_ = 0;
    expected_ = local;
    ++audioGeneration_;
    seekTarget_.store(local, std::memory_order_relaxed);
    generation_.store(audioGeneration_, std::memory_order_release);
}

std::span<const float> ClipStream::read(Frames local, Frames count) {
    if (local != expected_) {
        seek(local);
    }
    Frames produced = 0;
    const auto zero = [&](Frames frames) {
        std::fill_n(scratch_.begin() + produced * channels_, frames * channels_, 0.0F);
        produced += frames;
        expected_ += frames;
    };
    while (produced < count) {
        if (remaining_ == 0) {
            Header header;
            if (!readValue(ring_, header)) {
                break;
            }
            if (header.generation != audioGeneration_) {
                ring_.skip(bytesFor(header.frames));
                continue;
            }
            recordAt_ = header.start;
            remaining_ = header.frames;
        }
        if (recordAt_ < expected_) {
            const Frames late = std::min(remaining_, expected_ - recordAt_);
            ring_.skip(bytesFor(late));
            remaining_ -= late;
            recordAt_ += late;
            continue;
        }
        if (recordAt_ > expected_) {
            zero(std::min(count - produced, recordAt_ - expected_));
            continue;
        }
        const Frames frames = std::min(remaining_, count - produced);
        readInto(ring_, std::span(scratch_).subspan(samplesFor(produced), samplesFor(frames)));
        produced += frames;
        expected_ += frames;
        remaining_ -= frames;
        recordAt_ += frames;
    }
    zero(count - produced);
    return std::span(scratch_).first(samplesFor(count));
}

void ClipStream::poll() {
    const std::uint32_t generation = generation_.load(std::memory_order_acquire);
    if (generation != readerGeneration_) {
        readerGeneration_ = generation;
        next_ = std::clamp<Frames>(seekTarget_.load(std::memory_order_relaxed), 0, length_);
        sf_seek(file_.get(), sourceOffset_ + next_, SEEK_SET);
    }
    const auto current = [&] { return generation_.load(std::memory_order_acquire) == generation; };
    while (next_ < length_ && current()) {
        const Frames frames = std::min(kReadChunkFrames, length_ - next_);
        if (ring_.writable() < sizeof(Header) + bytesFor(frames)) {
            break;
        }
        const sf_count_t got = sf_readf_float(file_.get(), readBuffer_.data(), frames);
        if (got <= 0) {
            length_ = next_;
            break;
        }
        const Header header{generation, static_cast<std::uint32_t>(got), next_};
        writeRecord(ring_, header, std::span<const float>(readBuffer_).first(samplesFor(got)));
        next_ += got;
    }
    if (current()) {
        bufferedGeneration_.store(generation, std::memory_order_release);
    }
}

}  // namespace emerick::engine
