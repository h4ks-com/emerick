#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "emerick/engine/session.hpp"
#include "emerick/engine/sound_file.hpp"
#include "spsc_ring.hpp"

namespace emerick::engine {

class TakeInbox {
public:
    void add(RecordedTake take);
    std::vector<RecordedTake> take();

private:
    std::mutex mutex_;
    std::vector<RecordedTake> takes_;
};

// We call begin, the writes and end on the audio thread and poll on the writer thread, which writes WAV or collects
// MIDI. An audio take skips its first captureOffset frames, the round trip from our output back to our input, so it
// lines up with what the player heard. The UI thread reads the take in progress through takeProgress.
class TakeRecorder {
public:
    TakeRecorder(std::string trackId, TrackKind kind, int channels, double sampleRate, Frames maxBlock,
                 Frames captureOffset, std::filesystem::path folder, TakeInbox& inbox);
    ~TakeRecorder();
    TakeRecorder(const TakeRecorder&) = delete;
    TakeRecorder& operator=(const TakeRecorder&) = delete;
    TakeRecorder(TakeRecorder&&) = delete;
    TakeRecorder& operator=(TakeRecorder&&) = delete;

    [[nodiscard]] bool recording() const { return recording_; }
    // A positive delay starts an audio take with that many frames of silence, a negative one skips them.
    void begin(Ticks startTick, Frames delay);
    void writeAudio(std::span<const float* const> channels, Frames frames);
    void writeMidi(Ticks tick, std::span<const std::uint8_t> bytes);
    void countLostMidi(std::uint32_t events) { lostMidi_ += events; }
    void end();

    void poll();
    // What the take in progress gained since the last call, or nothing while no take runs.
    std::optional<TakeProgress> takeProgress();

private:
    enum class Kind : std::uint8_t { begin, audio, midi, end };
    // Audio frames we could not push are owed, and the writer writes them as silence before the next record.
    struct Header {
        Kind kind = Kind::begin;
        std::uint32_t size = 0;
        Ticks tick = 0;
        Frames owedFrames = 0;
        Frames leadFrames = 0;
    };

    template <class T>
    bool writeKeepingRoomForEnd(const Header& header, std::span<const T> payload);
    void start(Ticks startTick);
    void writeSilence(Frames frames);
    void writeZeros(Frames frames);
    void writeSamples(std::uint32_t frames);
    void writeToFile(std::uint32_t frames);
    void addPeaks(std::uint32_t frames, std::vector<float>& channelPeaks);
    void markDamaged(std::string error);
    void finish();

    std::string trackId_;
    TrackKind trackKind_;
    int channels_;
    double sampleRate_;
    Frames captureOffset_;
    std::filesystem::path folder_;
    TakeInbox& inbox_;
    SpscRing ring_;

    bool recording_ = false;
    Frames framesToSkip_ = 0;
    Frames owedFrames_ = 0;
    std::uint32_t lostMidi_ = 0;
    std::vector<float> interleaved_;

    std::optional<RecordedTake> take_;
    SndFile file_;
    std::vector<float> samples_;
    std::vector<std::uint8_t> bytes_;

    std::uint64_t takes_ = 0;
    Frames peakFrames_ = 0;
    float peakLow_ = 0.0F;
    float peakHigh_ = 0.0F;
    std::mutex progressMutex_;
    std::optional<TakeProgress> progress_;
};

}  // namespace emerick::engine
